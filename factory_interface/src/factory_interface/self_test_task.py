import asyncio
import json
from dataclasses import dataclass, field
from urllib.request import Request, urlopen

from factory_interface.network_discovery import get_find_device_task


SELF_TEST_POLL_SECONDS = 1.0
HTTP_TIMEOUT_SECONDS = 5.0


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


self_test_task = SelfTestTask()


def get_self_test_task() -> SelfTestTask:
    return self_test_task


def reset_self_test_task() -> None:
    self_test_task.status = "idle"
    self_test_task.details = ""
    self_test_task.result = None
    self_test_task.worker_task = None


def cancel_self_test_task() -> SelfTestTask:
    task = get_self_test_task()
    if task.status != "running":
        return task

    task.status = "failure"
    task.details = "Interactive self test task was cancelled."
    if task.worker_task is not None:
        task.worker_task.cancel()
    return task


def device_self_test_url() -> str:
    discovery_task = get_find_device_task()
    if discovery_task.status != "success" or discovery_task.device is None:
        raise RuntimeError("Device has not been discovered on the network.")

    device = discovery_task.device
    return f"http://{device.ip_address}:{device.port}/self-test"


def fetch_json(url: str, *, method: str = "GET") -> dict:
    request = Request(url, method=method)
    with urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


def status_result(payload: dict) -> str | None:
    status = str(payload.get("status", "")).lower()
    results = payload.get("results") if isinstance(payload.get("results"), dict) else {}
    all_tests = str(results.get("all_tests", "")).lower()

    if all_tests in {"pass", "success"} or status in {"pass", "success"}:
        return "success"
    if all_tests in {"fail", "failure"} or status in {"fail", "failure"}:
        return "failure"
    return None


async def run_interactive_self_test() -> None:
    task = get_self_test_task()

    async with task.lock:
        task.status = "running"
        task.details = "Starting interactive self test..."
        task.result = None

        try:
            base_url = device_self_test_url()
            await asyncio.to_thread(fetch_json, f"{base_url}/interactive", method="POST")

            while True:
                payload = await asyncio.to_thread(fetch_json, base_url)
                task.result = payload
                task.details = json.dumps(payload, indent=2)

                result = status_result(payload)
                if result is not None:
                    task.status = result
                    return

                if not payload.get("running", False):
                    task.status = "failure"
                    task.details = "Self test stopped without a pass/fail result.\n\n" + task.details
                    return

                await asyncio.sleep(SELF_TEST_POLL_SECONDS)
        except asyncio.CancelledError:
            task.status = "failure"
            task.details = "Interactive self test task was cancelled."
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
    task.details = "Starting interactive self test..."
    task.result = None
    task.worker_task = asyncio.create_task(run_interactive_self_test())
    return task
