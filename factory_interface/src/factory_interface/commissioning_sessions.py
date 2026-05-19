import asyncio
import json
import socket
import time
from dataclasses import dataclass, field
from datetime import datetime
from urllib.error import URLError
from urllib.request import Request, urlopen

from sqlmodel import Session, select

from factory_interface.database import engine
from factory_interface.fanet_id_task import (
    FANET_ID_MAX,
    FANET_ID_MIN,
    LEAF_MANUFACTURER_ID,
    most_recent_fanet_id_for_mac_address,
    normalize_mac_address,
)
from factory_interface.models import ConfigurationEvent
from factory_interface.network_discovery import LeafDiscoveryResponse, probe_once


HTTP_TIMEOUT_SECONDS = 5.0
RECONNECT_GRACE_SECONDS = 2.0
RECONNECT_TIMEOUT_SECONDS = 45.0
RECONNECT_POLL_SECONDS = 1.0
SELF_TEST_POLL_SECONDS = 1.0


def idle_task(details: str = "") -> dict:
    return {"status": "idle", "details": details}


@dataclass
class CommissioningSession:
    mac_address: str
    operator: str
    notes: str
    device: LeafDiscoveryResponse
    preflight: dict
    status: str = "running"
    details: str = "Commissioning started."
    created_at: datetime = field(default_factory=datetime.now)
    updated_at: datetime = field(default_factory=datetime.now)
    firmware_version: str | None = None
    fanet_id: int | None = None
    fanet_address: str | None = None
    self_test_result: dict | None = None
    configuration_event_id: int | None = None
    tasks: dict[str, dict] = field(default_factory=dict)
    worker_task: asyncio.Task | None = None

    def __post_init__(self) -> None:
        if self.tasks:
            return
        self.tasks = {
            "network_discovery": {
                "status": "success",
                "details": f"IP address: {self.device.ip_address}:{self.device.port}",
                "device": self.device.snapshot(),
            },
            "reset_nonvolatile_memory": {
                "status": "idle",
                "details": "",
                "reset_status": "idle",
                "reset_details": "",
                "reconnect_status": "idle",
                "reconnect_details": "",
            },
            "firmware_version": idle_task(),
            "fanet_id": idle_task(),
            "interactive_self_test": idle_task(),
            "persist_results": idle_task(),
        }

    def touch(self) -> None:
        self.updated_at = datetime.now()

    def snapshot(self) -> dict:
        return {
            "mac_address": self.mac_address,
            "operator": self.operator,
            "notes": self.notes,
            "status": self.status,
            "details": self.details,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
            "device": self.device.snapshot(),
            "firmware_version": self.firmware_version,
            "fanet_id": self.fanet_id,
            "fanet_address": self.fanet_address,
            "configuration_event_id": self.configuration_event_id,
            "preflight": self.preflight,
            "tasks": self.tasks,
        }


commissioning_sessions: dict[str, CommissioningSession] = {}
fanet_assignment_lock = asyncio.Lock()


def session_key(mac_address: str) -> str:
    return normalize_mac_address(mac_address)


def device_base_url(session: CommissioningSession) -> str:
    return f"http://{session.device.ip_address}:{session.device.port}"


def fetch_json(url: str, *, method: str = "GET") -> dict:
    request = Request(url, method=method)
    with urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


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


def response_matches_session(
    response: LeafDiscoveryResponse,
    session: CommissioningSession,
) -> bool:
    expected = session_key(session.mac_address)
    candidates = [response.mac_address, response.device_id]
    return any(session_key(candidate or "") == expected for candidate in candidates)


async def rediscover_session_device(session: CommissioningSession) -> LeafDiscoveryResponse:
    for response in await probe_once():
        if response_matches_session(response, session):
            session.device = response
            session.tasks["network_discovery"] = {
                "status": "success",
                "details": f"IP address: {response.ip_address}:{response.port}",
                "device": response.snapshot(),
            }
            session.touch()
            return response

    raise RuntimeError("Device was not found on the network.")


async def read_session_mac_address(session: CommissioningSession) -> str:
    payload = await asyncio.to_thread(fetch_json, f"{device_base_url(session)}/mac-address")
    mac_address = str(payload.get("mac_address", "")).strip()
    if not mac_address:
        raise RuntimeError("Device response did not include a MAC address.")
    if session_key(mac_address) != session_key(session.mac_address):
        raise RuntimeError(
            f"Device MAC address changed from {session.mac_address} to {mac_address}."
        )
    return mac_address


async def wait_for_session_device(session: CommissioningSession) -> None:
    deadline = time.monotonic() + RECONNECT_TIMEOUT_SECONDS
    last_error: Exception | None = None

    while time.monotonic() < deadline:
        try:
            await read_session_mac_address(session)
            return
        except (OSError, URLError, TimeoutError, RuntimeError) as exc:
            last_error = exc

        try:
            await rediscover_session_device(session)
            await read_session_mac_address(session)
            return
        except (OSError, URLError, TimeoutError, RuntimeError) as exc:
            last_error = exc

        await asyncio.sleep(RECONNECT_POLL_SECONDS)

    if last_error is not None:
        raise RuntimeError(f"Device did not reconnect after reset: {last_error}")
    raise RuntimeError("Device did not reconnect after reset.")


def active_fanet_ids(except_mac_address: str | None = None) -> set[int]:
    excluded_key = session_key(except_mac_address or "")
    used_ids = set()
    for session in commissioning_sessions.values():
        if excluded_key and session_key(session.mac_address) == excluded_key:
            continue
        if session.fanet_id is not None:
            used_ids.add(session.fanet_id)
    return used_ids


def next_available_fanet_id(temporary_ids: set[int]) -> int:
    with Session(engine) as db_session:
        used_ids = set(
            db_session.exec(
                select(ConfigurationEvent.fanet_id).where(
                    ConfigurationEvent.fanet_id >= FANET_ID_MIN,
                    ConfigurationEvent.fanet_id <= FANET_ID_MAX,
                )
            ).all()
        )

    used_ids.update(temporary_ids)

    for suffix in range(1, 0x10000):
        candidate = (LEAF_MANUFACTURER_ID << 16) | suffix
        if candidate not in used_ids:
            return candidate

    raise RuntimeError("No FANET IDs are available for Leaf manufacturer ID 0x0C.")


async def fanet_id_for_session(session: CommissioningSession) -> int:
    async with fanet_assignment_lock:
        existing_fanet_id = await asyncio.to_thread(
            most_recent_fanet_id_for_mac_address,
            session.mac_address,
        )
        if existing_fanet_id is not None:
            session.fanet_id = existing_fanet_id
            return existing_fanet_id

        fanet_id = await asyncio.to_thread(
            next_available_fanet_id,
            active_fanet_ids(except_mac_address=session.mac_address),
        )
        session.fanet_id = fanet_id
        return fanet_id


def self_test_status(payload: dict) -> str | None:
    status = str(payload.get("status", "")).lower()
    results = payload.get("results") if isinstance(payload.get("results"), dict) else {}
    all_tests = str(results.get("all_tests", "")).lower()

    if all_tests in {"pass", "success"} or status in {"pass", "success"}:
        return "success"
    if all_tests in {"fail", "failure"} or status in {"fail", "failure"}:
        return "failure"
    return None


def persist_configuration_event(session: CommissioningSession) -> int:
    event = ConfigurationEvent(
        mac_address=session.mac_address,
        operator=session.operator,
        configured_at=datetime.now(),
        configuration_action="setup",
        machine=socket.gethostname(),
        repo_state="not recorded",
        fanet_id=session.fanet_id,
        notes=session.notes or None,
        test_results=json.dumps(
            {
                "firmware_version": session.firmware_version,
                "self_test": session.self_test_result,
            },
            indent=2,
        ),
    )
    with Session(engine) as db_session:
        db_session.add(event)
        db_session.commit()
        db_session.refresh(event)
        return event.id or 0


async def run_commissioning_session(session: CommissioningSession) -> None:
    try:
        reset_task = session.tasks["reset_nonvolatile_memory"]
        reset_task.update(
            {
                "status": "running",
                "details": "Resetting nonvolatile memory...",
                "reset_status": "running",
                "reset_details": "Resetting nonvolatile memory...",
                "reconnect_status": "idle",
                "reconnect_details": "",
            }
        )
        session.touch()
        payload = await asyncio.to_thread(
            fetch_json,
            f"{device_base_url(session)}/settings/factory-reset",
            method="POST",
        )
        if not payload.get("reset_requested", False):
            raise RuntimeError("Device did not acknowledge the reset request.")

        reset_task.update(
            {
                "reset_status": "success",
                "reset_details": "Nonvolatile memory reset command accepted.",
                "reconnect_status": "running",
                "reconnect_details": "Waiting for device to reconnect...",
                "details": "Waiting for device to reboot after settings reset...",
            }
        )
        session.touch()
        await asyncio.sleep(RECONNECT_GRACE_SECONDS)
        await wait_for_session_device(session)
        reset_task.update(
            {
                "status": "success",
                "details": "Nonvolatile memory reset.",
                "reconnect_status": "success",
                "reconnect_details": "Device reconnected.",
            }
        )

        mac_address = await read_session_mac_address(session)
        session.mac_address = mac_address
        session.tasks["network_discovery"]["details"] = (
            f"IP address: {session.device.ip_address}:{session.device.port}; "
            f"MAC address: {mac_address}"
        )

        firmware_task = session.tasks["firmware_version"]
        firmware_task.update({"status": "running", "details": "Reading firmware version..."})
        session.touch()
        firmware_payload = await asyncio.to_thread(
            fetch_json,
            f"{device_base_url(session)}/firmware-version",
        )
        firmware_version = str(firmware_payload.get("firmware_version", "")).strip()
        if not firmware_version:
            raise RuntimeError("Device response did not include a firmware version.")
        session.firmware_version = firmware_version
        firmware_task.update(
            {
                "status": "success",
                "details": f"Firmware version: {firmware_version}",
                "firmware_version": firmware_version,
            }
        )

        fanet_task = session.tasks["fanet_id"]
        fanet_task.update({"status": "running", "details": "Assigning FANET ID..."})
        session.touch()
        fanet_id = await fanet_id_for_session(session)
        fanet_address = f"{fanet_id:06X}"
        fanet_payload = await asyncio.to_thread(
            post_json,
            f"{device_base_url(session)}/settings/fanet-address",
            {"fanet_address": fanet_address},
        )
        saved_fanet_address = str(fanet_payload.get("fanet_address", "")).strip().upper()
        if saved_fanet_address != fanet_address:
            raise RuntimeError("Device did not confirm the requested FANET address.")
        session.fanet_address = fanet_address
        fanet_task.update(
            {
                "status": "success",
                "details": f"FANET ID: {fanet_address} ({fanet_id} decimal)",
                "fanet_id": fanet_id,
                "fanet_address": fanet_address,
            }
        )

        self_test_task = session.tasks["interactive_self_test"]
        self_test_task.update(
            {"status": "running", "details": "Starting interactive self test..."}
        )
        session.touch()
        base_url = f"{device_base_url(session)}/self-test"
        await asyncio.to_thread(fetch_json, f"{base_url}/interactive", method="POST")

        while True:
            self_test_payload = await asyncio.to_thread(fetch_json, base_url)
            session.self_test_result = self_test_payload
            self_test_task["result"] = self_test_payload
            self_test_task["details"] = json.dumps(self_test_payload, indent=2)
            session.touch()

            result = self_test_status(self_test_payload)
            if result is not None:
                self_test_task["status"] = result
                if result == "failure":
                    raise RuntimeError("Interactive self test failed.")
                break

            if not self_test_payload.get("running", False):
                self_test_task["status"] = "failure"
                raise RuntimeError("Self test stopped without a pass/fail result.")

            await asyncio.sleep(SELF_TEST_POLL_SECONDS)

        persist_task = session.tasks["persist_results"]
        persist_task.update({"status": "running", "details": "Saving commissioning results..."})
        session.touch()
        event_id = await asyncio.to_thread(persist_configuration_event, session)
        session.configuration_event_id = event_id
        persist_task.update(
            {
                "status": "success",
                "details": f"Commissioning results saved as event {event_id}.",
            }
        )
        session.status = "success"
        session.details = "Commissioning complete."
    except asyncio.CancelledError:
        mark_running_task_failed(session, "Commissioning session was cancelled.")
        session.status = "failure"
        session.details = "Commissioning session was cancelled."
    except Exception as exc:
        mark_running_task_failed(session, f"{type(exc).__name__}: {exc}")
        session.status = "failure"
        session.details = f"{type(exc).__name__}: {exc}"
    finally:
        session.worker_task = None
        session.touch()


def mark_running_task_failed(session: CommissioningSession, details: str) -> None:
    for task in session.tasks.values():
        if task.get("status") != "running":
            continue
        task["status"] = "failure"
        task["details"] = details
        if task.get("reset_status") == "running":
            task["reset_status"] = "failure"
            task["reset_details"] = details
        if task.get("reconnect_status") == "running":
            task["reconnect_status"] = "failure"
            task["reconnect_details"] = details
        return


def create_commissioning_session(
    *,
    mac_address: str,
    operator: str,
    notes: str,
    device: LeafDiscoveryResponse,
    preflight: dict,
) -> CommissioningSession:
    key = session_key(mac_address)
    if not key:
        raise ValueError("MAC address is required.")

    existing = commissioning_sessions.get(key)
    if existing is not None:
        if existing.status == "running":
            return existing
        commissioning_sessions.pop(key, None)

    session = CommissioningSession(
        mac_address=mac_address,
        operator=operator,
        notes=notes,
        device=device,
        preflight=preflight,
    )
    commissioning_sessions[key] = session
    session.worker_task = asyncio.create_task(run_commissioning_session(session))
    return session


def get_commissioning_session(mac_address: str) -> CommissioningSession | None:
    return commissioning_sessions.get(session_key(mac_address))


def list_commissioning_sessions() -> list[CommissioningSession]:
    return sorted(
        commissioning_sessions.values(),
        key=lambda session: session.created_at,
        reverse=True,
    )


def cancel_commissioning_session(mac_address: str) -> CommissioningSession | None:
    session = get_commissioning_session(mac_address)
    if session is None:
        return None
    if session.worker_task is not None:
        session.worker_task.cancel()
    else:
        session.status = "failure"
        session.details = "Commissioning session was cancelled."
        mark_running_task_failed(session, session.details)
        session.touch()
    return session


def discard_commissioning_session(mac_address: str) -> bool:
    session = get_commissioning_session(mac_address)
    if session is None:
        return False
    if session.worker_task is not None:
        session.worker_task.cancel()
    commissioning_sessions.pop(session_key(mac_address), None)
    return True
