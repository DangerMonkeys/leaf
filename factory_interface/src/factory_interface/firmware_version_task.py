import asyncio
import json
from dataclasses import dataclass, field
from urllib.request import Request, urlopen

from factory_interface.network_discovery import get_find_device_task


HTTP_TIMEOUT_SECONDS = 5.0


@dataclass
class FirmwareVersionTask:
    status: str = "idle"
    firmware_version: str | None = None
    details: str = ""
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    worker_task: asyncio.Task | None = None

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "firmware_version": self.firmware_version,
            "details": self.details,
        }


firmware_version_task = FirmwareVersionTask()


def get_firmware_version_task() -> FirmwareVersionTask:
    return firmware_version_task


def reset_firmware_version_task() -> None:
    firmware_version_task.status = "idle"
    firmware_version_task.firmware_version = None
    firmware_version_task.details = ""
    firmware_version_task.worker_task = None


def cancel_firmware_version_task() -> FirmwareVersionTask:
    task = get_firmware_version_task()
    if task.status != "running":
        return task

    task.status = "failure"
    task.details = "Firmware version read task was cancelled."
    if task.worker_task is not None:
        task.worker_task.cancel()
    return task


def device_firmware_version_url() -> str:
    discovery_task = get_find_device_task()
    if discovery_task.status != "success" or discovery_task.device is None:
        raise RuntimeError("Device has not been discovered on the network.")

    device = discovery_task.device
    return f"http://{device.ip_address}:{device.port}/firmware-version"


def fetch_json(url: str) -> dict:
    request = Request(url, method="GET")
    with urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


async def run_read_firmware_version() -> None:
    task = get_firmware_version_task()

    async with task.lock:
        task.status = "running"
        task.firmware_version = None
        task.details = "Reading firmware version..."

        try:
            payload = await asyncio.to_thread(fetch_json, device_firmware_version_url())
            firmware_version = str(payload.get("firmware_version", "")).strip()
            if not firmware_version:
                raise RuntimeError("Device response did not include a firmware version.")

            task.firmware_version = firmware_version
            task.details = f"Firmware version: {firmware_version}"
            task.status = "success"
        except asyncio.CancelledError:
            task.status = "failure"
            task.details = "Firmware version read task was cancelled."
        except Exception as exc:
            task.status = "failure"
            task.details = f"{type(exc).__name__}: {exc}"
        finally:
            task.worker_task = None


def start_read_firmware_version() -> FirmwareVersionTask:
    task = get_firmware_version_task()
    if task.status == "running":
        return task

    task.status = "running"
    task.firmware_version = None
    task.details = "Reading firmware version..."
    task.worker_task = asyncio.create_task(run_read_firmware_version())
    return task
