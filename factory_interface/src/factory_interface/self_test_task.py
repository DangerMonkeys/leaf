import asyncio
import json
import time
from dataclasses import dataclass, field
from urllib.error import URLError
from urllib.request import Request

from factory_interface.http_client import (
    DEFAULT_HTTP_TIMEOUT_SECONDS,
    urlopen_with_timeout_retries,
)
from factory_interface.network_discovery import get_find_device_task


SELF_TEST_POLL_SECONDS = 1.0
HTTP_TIMEOUT_SECONDS = DEFAULT_HTTP_TIMEOUT_SECONDS
SELF_TEST_HTTP_TIMEOUT_SECONDS = 20.0
SELF_TEST_COMMUNICATION_GRACE_SECONDS = 180.0


@dataclass
class SelfTestTask:
    status: str = "idle"
    details: str = ""
    result: dict | None = None
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    worker_task: asyncio.Task | None = None

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "details": self.details,
            "result": self.result,
        }


@dataclass
class SelfTestDetailsTask:
    status: str = "idle"
    details: str = ""
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    worker_task: asyncio.Task | None = None

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "details": self.details,
        }


self_test_task = SelfTestTask()
self_test_details_task = SelfTestDetailsTask()


def get_self_test_task() -> SelfTestTask:
    return self_test_task


def get_self_test_details_task() -> SelfTestDetailsTask:
    return self_test_details_task


def reset_self_test_task() -> None:
    self_test_task.status = "idle"
    self_test_task.details = ""
    self_test_task.result = None
    self_test_task.worker_task = None


def reset_self_test_details_task() -> None:
    self_test_details_task.status = "idle"
    self_test_details_task.details = ""
    self_test_details_task.worker_task = None


def cancel_self_test_task() -> SelfTestTask:
    task = get_self_test_task()
    if task.status != "running":
        return task

    task.status = "failure"
    task.details = "Verification tests were cancelled."
    if task.worker_task is not None:
        task.worker_task.cancel()
    return task


def cancel_self_test_details_task() -> SelfTestDetailsTask:
    task = get_self_test_details_task()
    if task.status != "running":
        return task

    task.status = "failure"
    task.details = "Self test details retrieval was cancelled."
    if task.worker_task is not None:
        task.worker_task.cancel()
    return task


def device_self_test_url() -> str:
    discovery_task = get_find_device_task()
    if discovery_task.status != "success" or discovery_task.device is None:
        raise RuntimeError("Device has not been discovered on the network.")

    device = discovery_task.device
    return f"http://{device.ip_address}:{device.port}/self-test"


def fetch_json(
    url: str,
    *,
    method: str = "GET",
    timeout: float = HTTP_TIMEOUT_SECONDS,
) -> dict:
    request = Request(url, method=method)
    with urlopen_with_timeout_retries(
        request,
        timeout=timeout,
    ) as response:
        return json.loads(response.read().decode("utf-8"))


def fetch_text(
    url: str,
    *,
    method: str = "GET",
    timeout: float = HTTP_TIMEOUT_SECONDS,
) -> str:
    request = Request(url, method=method)
    with urlopen_with_timeout_retries(
        request,
        timeout=timeout,
    ) as response:
        return response.read().decode("utf-8")


def status_result(payload: dict) -> str | None:
    status = str(payload.get("status", "")).lower()
    results = payload.get("results") if isinstance(payload.get("results"), dict) else {}
    all_tests = str(results.get("all_tests", "")).lower()

    if all_tests in {"pass", "success"} or status in {"pass", "success"}:
        return "success"
    if all_tests in {"fail", "failure"} or status in {"fail", "failure"}:
        return "failure"
    return None


async def retrieve_self_test_details_text(base_url: str) -> str:
    details = await asyncio.to_thread(
        fetch_text,
        f"{base_url}/details",
        timeout=SELF_TEST_HTTP_TIMEOUT_SECONDS,
    )
    if not details.strip():
        raise RuntimeError("Device returned empty self test details.")
    return details


async def run_interactive_self_test() -> None:
    task = get_self_test_task()

    async with task.lock:
        task.status = "running"
        task.details = "Starting verification tests..."
        task.result = None

        try:
            base_url = device_self_test_url()
            await asyncio.to_thread(
                fetch_json,
                f"{base_url}/interactive",
                method="POST",
                timeout=SELF_TEST_HTTP_TIMEOUT_SECONDS,
            )
            last_device_response_at = time.monotonic()

            while True:
                try:
                    payload = await asyncio.to_thread(
                        fetch_json,
                        base_url,
                        timeout=SELF_TEST_HTTP_TIMEOUT_SECONDS,
                    )
                    last_device_response_at = time.monotonic()
                except (OSError, URLError, TimeoutError) as poll_exc:
                    elapsed = time.monotonic() - last_device_response_at
                    if elapsed >= SELF_TEST_COMMUNICATION_GRACE_SECONDS:
                        raise

                    remaining = int(SELF_TEST_COMMUNICATION_GRACE_SECONDS - elapsed)
                    task.details = (
                        "Waiting for device self-test response. The device may be busy "
                        "formatting the SD card.\n\n"
                        f"Last error: {type(poll_exc).__name__}: {poll_exc}\n"
                        f"Will keep waiting for {remaining} more seconds."
                    )
                    await asyncio.sleep(SELF_TEST_POLL_SECONDS)
                    continue

                task.result = payload
                task.details = json.dumps(payload, indent=2)

                result = status_result(payload)
                if result is not None:
                    task.status = result
                    if result == "failure":
                        try:
                            task.details = await retrieve_self_test_details_text(base_url)
                        except Exception as details_exc:
                            task.details = (
                                "Verification tests failed.\n\n"
                                f"Test details could not be retrieved: "
                                f"{type(details_exc).__name__}: {details_exc}"
                            )
                    return

                if not payload.get("running", False):
                    task.status = "failure"
                    try:
                        task.details = await retrieve_self_test_details_text(base_url)
                    except Exception as details_exc:
                        task.details = (
                            "Verification tests stopped without a pass/fail result.\n\n"
                            f"Test details could not be retrieved: "
                            f"{type(details_exc).__name__}: {details_exc}"
                        )
                    return

                await asyncio.sleep(SELF_TEST_POLL_SECONDS)
        except asyncio.CancelledError:
            task.status = "failure"
            task.details = "Verification tests were cancelled."
        except Exception as exc:
            task.status = "failure"
            task.details = f"{type(exc).__name__}: {exc}"
        finally:
            task.worker_task = None


def start_interactive_self_test() -> SelfTestTask:
    task = get_self_test_task()
    if task.status == "running":
        return task

    task.status = "running"
    task.details = "Starting verification tests..."
    task.result = None
    task.worker_task = asyncio.create_task(run_interactive_self_test())
    return task


async def run_retrieve_self_test_details() -> None:
    task = get_self_test_details_task()

    async with task.lock:
        task.status = "running"
        task.details = "Retrieving self test details..."

        try:
            details = await retrieve_self_test_details_text(device_self_test_url())
            task.status = "success"
            task.details = details
        except asyncio.CancelledError:
            task.status = "failure"
            task.details = "Self test details retrieval was cancelled."
        except Exception as exc:
            task.status = "failure"
            task.details = f"{type(exc).__name__}: {exc}"
        finally:
            task.worker_task = None


def start_retrieve_self_test_details() -> SelfTestDetailsTask:
    task = get_self_test_details_task()
    if task.status == "running":
        return task

    task.status = "running"
    task.details = "Retrieving self test details..."
    task.worker_task = asyncio.create_task(run_retrieve_self_test_details())
    return task
