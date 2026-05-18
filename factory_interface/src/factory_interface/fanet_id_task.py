import asyncio
import json
from dataclasses import dataclass, field
from urllib.request import Request, urlopen

from sqlmodel import Session, select

from factory_interface.database import engine
from factory_interface.models import ConfigurationEvent
from factory_interface.network_discovery import get_find_device_task


HTTP_TIMEOUT_SECONDS = 5.0
LEAF_MANUFACTURER_ID = 0x0C
FANET_ID_MIN = (LEAF_MANUFACTURER_ID << 16) | 0x0001
FANET_ID_MAX = (LEAF_MANUFACTURER_ID << 16) | 0xFFFF


@dataclass
class FanetIdTask:
    status: str = "idle"
    fanet_id: int | None = None
    fanet_address: str | None = None
    details: str = ""
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    worker_task: asyncio.Task | None = None

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "fanet_id": self.fanet_id,
            "fanet_address": self.fanet_address,
            "details": self.details,
        }


fanet_id_task = FanetIdTask()


def get_fanet_id_task() -> FanetIdTask:
    return fanet_id_task


def reset_fanet_id_task() -> None:
    fanet_id_task.status = "idle"
    fanet_id_task.fanet_id = None
    fanet_id_task.fanet_address = None
    fanet_id_task.details = ""
    fanet_id_task.worker_task = None


def cancel_fanet_id_task() -> FanetIdTask:
    task = get_fanet_id_task()
    if task.status != "running":
        return task

    task.status = "failure"
    task.details = "FANET ID assignment task was cancelled."
    if task.worker_task is not None:
        task.worker_task.cancel()
    return task


def next_available_fanet_id() -> int:
    with Session(engine) as session:
        used_ids = set(
            session.exec(
                select(ConfigurationEvent.fanet_id).where(
                    ConfigurationEvent.fanet_id >= FANET_ID_MIN,
                    ConfigurationEvent.fanet_id <= FANET_ID_MAX,
                )
            ).all()
        )

    for suffix in range(1, 0x10000):
        candidate = (LEAF_MANUFACTURER_ID << 16) | suffix
        if candidate not in used_ids:
            return candidate

    raise RuntimeError("No FANET IDs are available for Leaf manufacturer ID 0x0C.")


def device_fanet_address_url() -> str:
    discovery_task = get_find_device_task()
    if discovery_task.status != "success" or discovery_task.device is None:
        raise RuntimeError("Device has not been discovered on the network.")

    device = discovery_task.device
    return f"http://{device.ip_address}:{device.port}/settings/fanet-address"


def post_json(url: str, payload: dict) -> dict:
    data = json.dumps(payload).encode("utf-8")
    request = Request(
        url,
        data=data,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


async def run_assign_fanet_id() -> None:
    task = get_fanet_id_task()

    async with task.lock:
        task.status = "running"
        task.fanet_id = None
        task.fanet_address = None
        task.details = "Assigning FANET ID..."

        try:
            fanet_id = await asyncio.to_thread(next_available_fanet_id)
            fanet_address = f"{fanet_id:06X}"
            payload = await asyncio.to_thread(
                post_json,
                device_fanet_address_url(),
                {"fanet_address": fanet_address},
            )
            saved_fanet_address = str(payload.get("fanet_address", "")).strip().upper()
            if saved_fanet_address != fanet_address:
                raise RuntimeError("Device did not confirm the requested FANET address.")

            task.fanet_id = fanet_id
            task.fanet_address = fanet_address
            task.details = f"FANET ID: {fanet_address} ({fanet_id} decimal)"
            task.status = "success"
        except asyncio.CancelledError:
            task.status = "failure"
            task.details = "FANET ID assignment task was cancelled."
        except Exception as exc:
            task.status = "failure"
            task.details = f"{type(exc).__name__}: {exc}"
        finally:
            task.worker_task = None


def start_assign_fanet_id() -> FanetIdTask:
    task = get_fanet_id_task()
    if task.status == "running":
        return task

    task.status = "running"
    task.fanet_id = None
    task.fanet_address = None
    task.details = "Assigning FANET ID..."
    task.worker_task = asyncio.create_task(run_assign_fanet_id())
    return task
