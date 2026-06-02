import asyncio
import json
from dataclasses import dataclass, field
from urllib.error import URLError
from urllib.request import Request

from factory_interface.http_client import (
    DEFAULT_HTTP_TIMEOUT_SECONDS,
    urlopen_with_timeout_retries,
)
from factory_interface.network_discovery import get_find_device_task, probe_once


HTTP_TIMEOUT_SECONDS = DEFAULT_HTTP_TIMEOUT_SECONDS
RESET_REBOOT_GRACE_SECONDS = 2.0
RESET_RECONNECT_POLL_SECONDS = 1.0


@dataclass
class ResetNonvolatileMemoryTask:
    status: str = "idle"
    details: str = ""
    reset_status: str = "idle"
    reset_details: str = ""
    reconnect_status: str = "idle"
    reconnect_details: str = ""
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    worker_task: asyncio.Task | None = None

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "details": self.details,
            "reset_status": self.reset_status,
            "reset_details": self.reset_details,
            "reconnect_status": self.reconnect_status,
            "reconnect_details": self.reconnect_details,
        }


reset_nonvolatile_memory_task = ResetNonvolatileMemoryTask()


def get_reset_nonvolatile_memory_task() -> ResetNonvolatileMemoryTask:
    return reset_nonvolatile_memory_task


def reset_reset_nonvolatile_memory_task() -> None:
    reset_nonvolatile_memory_task.status = "idle"
    reset_nonvolatile_memory_task.details = ""
    reset_nonvolatile_memory_task.reset_status = "idle"
    reset_nonvolatile_memory_task.reset_details = ""
    reset_nonvolatile_memory_task.reconnect_status = "idle"
    reset_nonvolatile_memory_task.reconnect_details = ""
    reset_nonvolatile_memory_task.worker_task = None


def cancel_reset_nonvolatile_memory() -> ResetNonvolatileMemoryTask:
    task = get_reset_nonvolatile_memory_task()
    if task.status != "running":
        return task

    task.status = "failure"
    task.details = "Nonvolatile memory reset task was cancelled."
    if task.reset_status == "running":
        task.reset_status = "failure"
        task.reset_details = task.details
    elif task.reconnect_status == "running":
        task.reconnect_status = "failure"
        task.reconnect_details = task.details
    if task.worker_task is not None:
        task.worker_task.cancel()
    return task


def device_base_url() -> tuple[str, str]:
    discovery_task = get_find_device_task()
    if discovery_task.status != "success" or discovery_task.device is None:
        raise RuntimeError("Device has not been discovered on the network.")

    device = discovery_task.device
    return f"http://{device.ip_address}:{device.port}", device.device_id


def fetch_json(url: str, *, method: str = "GET") -> dict:
    request = Request(url, method=method)
    with urlopen_with_timeout_retries(
        request,
        timeout=HTTP_TIMEOUT_SECONDS,
    ) as response:
        return json.loads(response.read().decode("utf-8"))


async def wait_for_device(base_url: str, device_id: str) -> None:
    while True:
        try:
            await asyncio.to_thread(fetch_json, f"{base_url}/mac-address")
            return
        except (OSError, URLError, TimeoutError):
            pass

        selected_device = get_find_device_task().device
        if selected_device is not None and selected_device.device_id == device_id:
            try:
                await asyncio.to_thread(
                    fetch_json,
                    f"http://{selected_device.ip_address}:{selected_device.port}/mac-address",
                )
                return
            except (OSError, URLError, TimeoutError):
                pass

        try:
            for response in await probe_once():
                if response.device_id == device_id:
                    get_find_device_task().device = response
                    await asyncio.to_thread(
                        fetch_json,
                        f"http://{response.ip_address}:{response.port}/mac-address",
                    )
                    return
        except (OSError, URLError, TimeoutError):
            pass

        await asyncio.sleep(RESET_RECONNECT_POLL_SECONDS)


async def run_reset_nonvolatile_memory() -> None:
    task = get_reset_nonvolatile_memory_task()

    async with task.lock:
        task.status = "running"
        task.details = "Resetting nonvolatile memory..."
        task.reset_status = "running"
        task.reset_details = "Resetting nonvolatile memory..."
        task.reconnect_status = "idle"
        task.reconnect_details = ""

        try:
            base_url, device_id = device_base_url()
            payload = await asyncio.to_thread(
                fetch_json,
                f"{base_url}/settings/factory-reset",
                method="POST",
            )
            if not payload.get("reset_requested", False):
                raise RuntimeError("Device did not acknowledge the reset request.")

            task.reset_status = "success"
            task.reset_details = "Nonvolatile memory reset command accepted."
            task.reconnect_status = "running"
            task.reconnect_details = "Waiting for device to reconnect..."
            task.details = "Waiting for device to reboot after settings reset..."
            await asyncio.sleep(RESET_REBOOT_GRACE_SECONDS)
            await wait_for_device(base_url, device_id)

            task.status = "success"
            task.details = "Nonvolatile memory reset."
            task.reconnect_status = "success"
            task.reconnect_details = "Device reconnected."
        except asyncio.CancelledError:
            task.status = "failure"
            task.details = "Nonvolatile memory reset task was cancelled."
            if task.reset_status == "running":
                task.reset_status = "failure"
                task.reset_details = task.details
            elif task.reconnect_status == "running":
                task.reconnect_status = "failure"
                task.reconnect_details = task.details
        except Exception as exc:
            task.status = "failure"
            task.details = f"{type(exc).__name__}: {exc}"
            if task.reset_status == "running":
                task.reset_status = "failure"
                task.reset_details = task.details
            else:
                task.reconnect_status = "failure"
                task.reconnect_details = task.details
        finally:
            task.worker_task = None


def start_reset_nonvolatile_memory() -> ResetNonvolatileMemoryTask:
    task = get_reset_nonvolatile_memory_task()
    if task.status == "running":
        return task

    task.status = "running"
    task.details = "Resetting nonvolatile memory..."
    task.reset_status = "running"
    task.reset_details = "Resetting nonvolatile memory..."
    task.reconnect_status = "idle"
    task.reconnect_details = ""
    task.worker_task = asyncio.create_task(run_reset_nonvolatile_memory())
    return task
