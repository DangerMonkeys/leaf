import asyncio
import json
import secrets
import socket
from dataclasses import dataclass, field


DISCOVERY_PORT = 7432
DISCOVERY_REQUEST_PREFIX = "LEAF_DISCOVERY_REQUEST/1 "
DISCOVERY_RESPONSE_TYPE = "leaf_discovery_response"
PROBE_INTERVAL_SECONDS = 1.0
PROBE_LISTEN_SECONDS = 0.4


@dataclass(frozen=True)
class LeafDiscoveryResponse:
    ip_address: str
    port: int
    device_id: str
    nonce: str

    def snapshot(self) -> dict:
        return {
            "ip_address": self.ip_address,
            "port": self.port,
            "device_id": self.device_id,
        }


@dataclass
class FindDeviceTask:
    status: str = "idle"
    device: LeafDiscoveryResponse | None = None
    error: str | None = None
    excluded_device_ids: set[str] = field(default_factory=set)
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "device": self.device.snapshot() if self.device else None,
            "error": self.error,
        }


class LeafDiscoveryProtocol(asyncio.DatagramProtocol):
    def __init__(self, nonce: str):
        self.nonce = nonce
        self.responses: list[LeafDiscoveryResponse] = []

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        try:
            payload = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return

        if payload.get("type") != DISCOVERY_RESPONSE_TYPE:
            return
        if payload.get("nonce") != self.nonce:
            return

        device_id = str(payload.get("device_id", "")).strip()
        if not device_id:
            return

        try:
            port = int(payload.get("http_port", 0))
        except (TypeError, ValueError):
            return
        if port <= 0:
            return

        self.responses.append(
            LeafDiscoveryResponse(
                ip_address=addr[0],
                port=port,
                device_id=device_id,
                nonce=self.nonce,
            )
        )


find_device_tasks: dict[str, FindDeviceTask] = {}
preflash_devices: dict[str, set[str]] = {}
preflash_monitor_tasks: dict[str, asyncio.Task] = {}
SETUP_TASK_KEY = "setup"


def get_find_device_task() -> FindDeviceTask:
    if SETUP_TASK_KEY not in find_device_tasks:
        find_device_tasks[SETUP_TASK_KEY] = FindDeviceTask()
    return find_device_tasks[SETUP_TASK_KEY]


def reset_find_device_task() -> None:
    task = get_find_device_task()
    task.status = "idle"
    task.device = None
    task.error = None
    task.excluded_device_ids = set()
    preflash_devices.pop(SETUP_TASK_KEY, None)


async def probe_once() -> list[LeafDiscoveryResponse]:
    nonce = secrets.token_hex(8)
    loop = asyncio.get_running_loop()
    protocol = LeafDiscoveryProtocol(nonce)

    transport, _ = await loop.create_datagram_endpoint(
        lambda: protocol,
        local_addr=("0.0.0.0", 0),
        family=socket.AF_INET,
        allow_broadcast=True,
    )

    try:
        request = f"{DISCOVERY_REQUEST_PREFIX}{nonce}".encode("ascii")
        transport.sendto(request, ("255.255.255.255", DISCOVERY_PORT))
        await asyncio.sleep(PROBE_LISTEN_SECONDS)
        return protocol.responses
    finally:
        transport.close()


async def collect_existing_devices() -> None:
    preflash_devices[SETUP_TASK_KEY] = set()

    while True:
        try:
            for response in await probe_once():
                preflash_devices[SETUP_TASK_KEY].add(response.device_id)
        except OSError:
            pass
        await asyncio.sleep(PROBE_INTERVAL_SECONDS)


def start_preflash_monitor() -> None:
    stop_preflash_monitor()
    preflash_monitor_tasks[SETUP_TASK_KEY] = asyncio.create_task(collect_existing_devices())


def stop_preflash_monitor() -> None:
    task = preflash_monitor_tasks.pop(SETUP_TASK_KEY, None)
    if task is not None:
        task.cancel()


async def run_find_device() -> None:
    task = get_find_device_task()

    async with task.lock:
        task.status = "running"
        task.device = None
        task.error = None
        task.excluded_device_ids = set(preflash_devices.get(SETUP_TASK_KEY, set()))

        try:
            while True:
                for response in await probe_once():
                    if response.device_id in task.excluded_device_ids:
                        continue

                    task.device = response
                    task.status = "success"
                    return

                await asyncio.sleep(PROBE_INTERVAL_SECONDS)
        except asyncio.CancelledError:
            task.status = "idle"
            raise
        except Exception as exc:
            task.status = "failure"
            task.error = f"{type(exc).__name__}: {exc}"


def start_find_device() -> FindDeviceTask:
    task = get_find_device_task()
    if task.status == "running":
        return task

    task.status = "running"
    task.device = None
    task.error = None
    asyncio.create_task(run_find_device())
    return task
