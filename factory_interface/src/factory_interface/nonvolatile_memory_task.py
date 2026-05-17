import asyncio
import json
import time
from dataclasses import dataclass, field
from urllib.error import URLError
from urllib.request import Request, urlopen

from factory_interface.network_discovery import get_find_device_task, probe_once


HTTP_TIMEOUT_SECONDS = 5.0
RESET_REBOOT_GRACE_SECONDS = 2.0
RESET_RECONNECT_TIMEOUT_SECONDS = 45.0
RESET_RECONNECT_POLL_SECONDS = 1.0


@dataclass
class ResetNonvolatileMemoryTask:
    status: str = "idle"
    details: str = ""
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "details": self.details,
        }


reset_nonvolatile_memory_task = ResetNonvolatileMemoryTask()


def get_reset_nonvolatile_memory_task() -> ResetNonvolatileMemoryTask:
    return reset_nonvolatile_memory_task


def reset_reset_nonvolatile_memory_task() -> None:
    reset_nonvolatile_memory_task.status = "idle"
    reset_nonvolatile_memory_task.details = ""


def device_base_url() -> tuple[str, str]:
    discovery_task = get_find_device_task()
    if discovery_task.status != "success" or discovery_task.device is None:
        raise RuntimeError("Device has not been discovered on the network.")

    device = discovery_task.device
    return f"http://{device.ip_address}:{device.port}", device.device_id


def fetch_json(url: str, *, method: str = "GET") -> dict:
    request = Request(url, method=method)
    with urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


async def wait_for_device(base_url: str, device_id: str) -> None:
    deadline = time.monotonic() + RESET_RECONNECT_TIMEOUT_SECONDS
    last_error: Exception | None = None

    while time.monotonic() < deadline:
        try:
            await asyncio.to_thread(fetch_json, f"{base_url}/mac-address")
            return
        except (OSError, URLError, TimeoutError) as exc:
            last_error = exc

        try:
            for response in await probe_once():
                if response.device_id == device_id:
                    get_find_device_task().device = response
                    await asyncio.to_thread(
                        fetch_json,
                        f"http://{response.ip_address}:{response.port}/mac-address",
                    )
                    return
        except (OSError, URLError, TimeoutError) as exc:
            last_error = exc

        await asyncio.sleep(RESET_RECONNECT_POLL_SECONDS)

    if last_error is not None:
        raise RuntimeError(f"Device did not reconnect after reset: {last_error}")
    raise RuntimeError("Device did not reconnect after reset.")


async def run_reset_nonvolatile_memory() -> None:
    task = get_reset_nonvolatile_memory_task()

    async with task.lock:
        task.status = "running"
        task.details = "Resetting nonvolatile memory..."

        try:
            base_url, device_id = device_base_url()
            payload = await asyncio.to_thread(
                fetch_json,
                f"{base_url}/settings/factory-reset",
                method="POST",
            )
            if not payload.get("reset_requested", False):
                raise RuntimeError("Device did not acknowledge the reset request.")

            task.details = "Waiting for device to reboot after settings reset..."
            await asyncio.sleep(RESET_REBOOT_GRACE_SECONDS)
            await wait_for_device(base_url, device_id)

            task.status = "success"
            task.details = "Nonvolatile memory reset."
        except Exception as exc:
            task.status = "failure"
            task.details = f"{type(exc).__name__}: {exc}"


def start_reset_nonvolatile_memory() -> ResetNonvolatileMemoryTask:
    task = get_reset_nonvolatile_memory_task()
    if task.status == "running":
        return task

    task.status = "running"
    task.details = "Resetting nonvolatile memory..."
    asyncio.create_task(run_reset_nonvolatile_memory())
    return task
