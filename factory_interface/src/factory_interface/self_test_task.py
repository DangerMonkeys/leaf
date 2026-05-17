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

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "details": self.details,
            "result": self.result,
        }


self_test_tasks: dict[str, SelfTestTask] = {}


def get_self_test_task(serial_number: str) -> SelfTestTask:
    if serial_number not in self_test_tasks:
        self_test_tasks[serial_number] = SelfTestTask()
    return self_test_tasks[serial_number]


def device_self_test_url(serial_number: str) -> str:
    discovery_task = get_find_device_task(serial_number)
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


async def run_interactive_self_test(serial_number: str) -> None:
    task = get_self_test_task(serial_number)

    async with task.lock:
        task.status = "running"
        task.details = "Starting interactive self test..."
        task.result = None

        try:
            base_url = device_self_test_url(serial_number)
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
        except Exception as exc:
            task.status = "failure"
            task.details = f"{type(exc).__name__}: {exc}"


def start_interactive_self_test(serial_number: str) -> SelfTestTask:
    task = get_self_test_task(serial_number)
    if task.status == "running":
        return task

    task.status = "running"
    task.details = "Starting interactive self test..."
    task.result = None
    asyncio.create_task(run_interactive_self_test(serial_number))
    return task
