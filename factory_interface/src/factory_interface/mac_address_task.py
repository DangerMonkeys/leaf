import asyncio
import json
from dataclasses import dataclass, field
from urllib.request import Request, urlopen

from factory_interface.network_discovery import get_find_device_task


HTTP_TIMEOUT_SECONDS = 5.0


@dataclass
class MacAddressTask:
    status: str = "idle"
    mac_address: str | None = None
    details: str = ""
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "mac_address": self.mac_address,
            "details": self.details,
        }


mac_address_task = MacAddressTask()


def get_mac_address_task() -> MacAddressTask:
    return mac_address_task


def reset_mac_address_task() -> None:
    mac_address_task.status = "idle"
    mac_address_task.mac_address = None
    mac_address_task.details = ""


def device_mac_address_url() -> str:
    discovery_task = get_find_device_task()
    if discovery_task.status != "success" or discovery_task.device is None:
        raise RuntimeError("Device has not been discovered on the network.")

    device = discovery_task.device
    return f"http://{device.ip_address}:{device.port}/mac-address"


def fetch_json(url: str) -> dict:
    request = Request(url, method="GET")
    with urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


async def run_read_mac_address() -> None:
    task = get_mac_address_task()

    async with task.lock:
        task.status = "running"
        task.mac_address = None
        task.details = "Reading MAC address..."

        try:
            payload = await asyncio.to_thread(fetch_json, device_mac_address_url())
            mac_address = str(payload.get("mac_address", "")).strip()
            if not mac_address:
                raise RuntimeError("Device response did not include a MAC address.")

            task.mac_address = mac_address
            task.details = f"MAC address: {mac_address}"
            task.status = "success"
        except Exception as exc:
            task.status = "failure"
            task.details = f"{type(exc).__name__}: {exc}"


def start_read_mac_address() -> MacAddressTask:
    task = get_mac_address_task()
    if task.status == "running":
        return task

    task.status = "running"
    task.mac_address = None
    task.details = "Reading MAC address..."
    asyncio.create_task(run_read_mac_address())
    return task
