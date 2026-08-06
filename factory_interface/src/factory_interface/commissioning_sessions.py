import asyncio
import getpass
import hashlib
import json
import platform
import socket
import subprocess
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request

from sqlmodel import Session, select

from factory_interface.database import engine
from factory_interface.fanet_id_task import (
    FANET_ID_MAX,
    FANET_ID_MIN,
    LEAF_MANUFACTURER_ID,
    most_recent_fanet_id_for_mac_address,
    normalize_mac_address,
)
from factory_interface.http_client import (
    DEFAULT_HTTP_TIMEOUT_SECONDS,
    urlopen_with_timeout_retries,
)
from factory_interface.models import ConfigurationEvent, File
from factory_interface.network_discovery import (
    LeafDiscoveryResponse,
    discovery_identifier_values,
    normalize_discovery_identifier,
    probe_ip_once,
)
from factory_interface.settings import (
    FactoryInterfaceSettings,
    describe_application_firmware_source,
    describe_non_application_binary_path,
    load_settings,
    resolve_application_firmware_file,
)


PROJECT_ROOT = Path(__file__).resolve().parents[3]
HTTP_TIMEOUT_SECONDS = DEFAULT_HTTP_TIMEOUT_SECONDS
SELF_TEST_POLL_SECONDS = 1.0
SELF_TEST_HTTP_TIMEOUT_SECONDS = 20.0
SELF_TEST_COMMUNICATION_GRACE_SECONDS = 180.0
SD_FORMAT_HTTP_TIMEOUT_SECONDS = 180.0
REBOOT_GRACE_SECONDS = 2.0
RECONNECT_POLL_SECONDS = 1.0
RECONNECT_TIMEOUT_SECONDS = 120.0


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
    self_test_details: str | None = None
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
            "prepare_sd_card": idle_task(),
            "firmware_version": idle_task(),
            "fanet_id": idle_task(),
            "interactive_self_test": idle_task(),
            "retrieve_test_details": idle_task(),
            "persist_results": idle_task(),
            "clear_self_test_results": idle_task(),
            "notify_commissioning_complete": idle_task(),
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
            "self_test_details": self.self_test_details,
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


def fetch_json(
    url: str,
    *,
    method: str = "GET",
    timeout: float = HTTP_TIMEOUT_SECONDS,
) -> dict:
    request = Request(url, method=method)
    try:
        with urlopen_with_timeout_retries(
            request,
            timeout=timeout,
        ) as response:
            return json.loads(response.read().decode("utf-8"))
    except HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace").strip()
        detail = body or exc.reason
        raise RuntimeError(f"HTTP {exc.code} from {url}: {detail}") from exc


def fetch_text(
    url: str,
    *,
    method: str = "GET",
    timeout: float = HTTP_TIMEOUT_SECONDS,
) -> str:
    request = Request(url, method=method)
    try:
        with urlopen_with_timeout_retries(
            request,
            timeout=timeout,
        ) as response:
            return response.read().decode("utf-8")
    except HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace").strip()
        detail = body or exc.reason
        raise RuntimeError(f"HTTP {exc.code} from {url}: {detail}") from exc


def post_json(
    url: str,
    payload: dict,
    *,
    timeout: float = HTTP_TIMEOUT_SECONDS,
) -> dict:
    data = json.dumps(payload).encode("utf-8")
    request = Request(
        url,
        data=data,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urlopen_with_timeout_retries(
            request,
            timeout=timeout,
        ) as response:
            return json.loads(response.read().decode("utf-8"))
    except HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace").strip()
        detail = body or exc.reason
        raise RuntimeError(f"HTTP {exc.code} from {url}: {detail}") from exc


def response_matches_session(
    response: LeafDiscoveryResponse,
    session: CommissioningSession,
) -> bool:
    expected = session_key(session.mac_address)
    candidates = [response.mac_address, response.device_id]
    return any(session_key(candidate or "") == expected for candidate in candidates)


async def manually_rediscover_session_device(
    session: CommissioningSession,
    ip_address: str,
) -> LeafDiscoveryResponse:
    responses = await probe_ip_once(ip_address)
    if not responses:
        raise RuntimeError(f"No discovery response received from {ip_address}.")

    response = responses[0]
    if not response_matches_session(response, session):
        raise RuntimeError(
            "Discovery response did not match the commissioning session device."
        )

    session.device = response
    session.tasks["network_discovery"] = {
        "status": "success",
        "details": f"IP address: {response.ip_address}:{response.port}",
        "device": response.snapshot(),
    }
    session.touch()
    return response


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


def sd_preparation_details(payload: dict) -> str:
    stage = str(payload.get("stage", "unknown"))
    attempts = int(payload.get("mount_attempts", 0))
    error = str(payload.get("esp_error", "unknown"))
    return f"stage={stage}; mount attempts={attempts}; ESP error={error}"


async def prepare_session_sd_card(session: CommissioningSession) -> None:
    task = session.tasks["prepare_sd_card"]
    if not session.preflight.get("force_format_sd_card", False):
        task.update({"status": "success", "details": "SD card formatting disabled."})
        session.touch()
        return

    task.update({"status": "running", "details": "Formatting SD card..."})
    session.touch()
    payload = await asyncio.to_thread(
        post_json,
        f"{device_base_url(session)}/sd-card/format",
        {},
        timeout=SD_FORMAT_HTTP_TIMEOUT_SECONDS,
    )
    task["result"] = payload
    details = sd_preparation_details(payload)
    if not payload.get("formatted", False):
        raise RuntimeError(
            f"SD card format failed ({details})."
        )
    if not payload.get("mounted", False):
        raise RuntimeError(f"SD card formatted but did not remount ({details}).")
    if not payload.get("label_set", False):
        raise RuntimeError(f"SD card remounted but its label could not be set ({details}).")

    task.update(
        {
            "status": "success",
            "details": "SD card formatted and remounted.",
        }
    )
    session.touch()


async def remount_session_sd_card(session: CommissioningSession) -> None:
    task = session.tasks["prepare_sd_card"]
    task.update({"status": "running", "details": "Retrying SD card mount..."})
    session.touch()
    payload = await asyncio.to_thread(
        post_json,
        f"{device_base_url(session)}/sd-card/remount",
        {},
        timeout=SD_FORMAT_HTTP_TIMEOUT_SECONDS,
    )
    task["result"] = payload
    details = sd_preparation_details(payload)
    if not payload.get("mounted", False):
        raise RuntimeError(f"SD card still did not mount ({details}).")
    if not payload.get("label_set", False):
        raise RuntimeError(f"SD card mounted but its label could not be set ({details}).")
    task.update({"status": "success", "details": "SD card mounted successfully."})
    session.touch()


async def wait_for_session_device(session: CommissioningSession) -> None:
    deadline = time.monotonic() + RECONNECT_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        try:
            await read_session_mac_address(session)
            return
        except (OSError, URLError, TimeoutError, RuntimeError):
            pass

        try:
            responses = await probe_ip_once(session.device.ip_address)
            for response in responses:
                if response_matches_session(response, session):
                    session.device = response
                    await read_session_mac_address(session)
                    return
        except (OSError, URLError, TimeoutError, RuntimeError):
            pass
        await asyncio.sleep(RECONNECT_POLL_SECONDS)

    raise TimeoutError("Timed out waiting for the device to reconnect.")


async def restart_and_resume_session(session: CommissioningSession) -> None:
    task = session.tasks["prepare_sd_card"]
    try:
        task.update({"status": "running", "details": "Restarting Leaf..."})
        session.touch()
        payload = await asyncio.to_thread(
            post_json,
            f"{device_base_url(session)}/restart",
            {},
        )
        if not payload.get("restart_requested", False):
            raise RuntimeError("Device did not acknowledge the restart request.")
        await asyncio.sleep(REBOOT_GRACE_SECONDS)
        task["details"] = "Waiting for Leaf to reconnect..."
        session.touch()
        await wait_for_session_device(session)
        await remount_session_sd_card(session)
        await run_commissioning_session(session, sd_preparation="skip")
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


async def retrieve_session_self_test_details(session: CommissioningSession, base_url: str) -> str:
    self_test_details = await asyncio.to_thread(
        fetch_text,
        f"{base_url}/details",
        timeout=SELF_TEST_HTTP_TIMEOUT_SECONDS,
    )
    if not self_test_details.strip():
        raise RuntimeError("Device returned empty self test details.")
    session.self_test_details = self_test_details
    return self_test_details


async def notify_session_commissioning_complete(session: CommissioningSession) -> dict:
    payload = await asyncio.to_thread(
        fetch_json,
        f"{device_base_url(session)}/commissioning/complete",
        method="POST",
    )
    if not payload.get("commissioning_complete", False):
        raise RuntimeError("Device did not acknowledge commissioning completion.")
    return payload


async def clear_session_self_test_results(session: CommissioningSession) -> dict:
    payload = await asyncio.to_thread(
        fetch_json,
        f"{device_base_url(session)}/self-test/results",
        method="DELETE",
    )
    if not payload.get("cleared", False):
        raise RuntimeError("Device did not clear self test results.")
    return payload


def machine_description() -> str:
    return " ".join(
        [
            f"{getpass.getuser()}@{socket.gethostname()}",
            platform.system(),
            platform.version(),
            platform.machine(),
        ]
    )


def run_git_command(args: list[str]) -> str:
    result = subprocess.run(
        ["git", "-C", str(PROJECT_ROOT), *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def repo_state() -> str:
    commit_hash = run_git_command(["rev-parse", "HEAD"])
    porcelain_status = run_git_command(["status", "--porcelain"])
    if not porcelain_status:
        return commit_hash
    return commit_hash + "\n" + run_git_command(["status"])


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path, source: str) -> File:
    stat = path.stat()
    return File(
        name=path.name,
        source=source,
        size=stat.st_size,
        sha256=sha256_file(path),
        modified_at=datetime.fromtimestamp(stat.st_mtime),
    )


def firmware_file_sources(
    settings: FactoryInterfaceSettings | None = None,
) -> dict[str, dict[str, str]]:
    settings = settings or load_settings()
    application_source = describe_application_firmware_source(
        settings.application_firmware_source,
        settings,
    )
    non_application_source = describe_non_application_binary_path(
        settings.non_application_firmware_path,
    )
    if settings.non_application_firmware_path is None:
        raise RuntimeError("Non-application binaries are not configured.")

    non_application_path = Path(settings.non_application_firmware_path)
    return {
        "firmware": {
            "path": str(resolve_application_firmware_file(settings)),
            "source": application_source,
        },
        "bootloader": {
            "path": str(non_application_path / "bootloader.bin"),
            "source": non_application_source,
        },
        "partitions": {
            "path": str(non_application_path / "partitions.bin"),
            "source": non_application_source,
        },
    }


def firmware_file_records(session: CommissioningSession) -> dict[str, File]:
    sources = session.preflight.get("firmware_files")
    if not isinstance(sources, dict):
        sources = firmware_file_sources()

    return {
        "firmware": file_record(
            Path(sources["firmware"]["path"]),
            str(sources["firmware"]["source"]),
        ),
        "bootloader": file_record(
            Path(sources["bootloader"]["path"]),
            str(sources["bootloader"]["source"]),
        ),
        "partitions": file_record(
            Path(sources["partitions"]["path"]),
            str(sources["partitions"]["source"]),
        ),
    }


def get_or_create_file(db_session: Session, file_record: File) -> int:
    existing_file = db_session.exec(
        select(File).where(
            File.name == file_record.name,
            File.source == file_record.source,
            File.size == file_record.size,
            File.sha256 == file_record.sha256,
            File.modified_at == file_record.modified_at,
        )
    ).first()
    if existing_file is not None and existing_file.id is not None:
        return existing_file.id

    db_session.add(file_record)
    db_session.flush()
    if file_record.id is None:
        raise RuntimeError(f"File row was not assigned an ID: {file_record.name}")
    return file_record.id


def persist_configuration_event(session: CommissioningSession) -> int:
    file_records = firmware_file_records(session)
    with Session(engine) as db_session:
        firmware_id = get_or_create_file(db_session, file_records["firmware"])
        bootloader_id = get_or_create_file(db_session, file_records["bootloader"])
        partitions_id = get_or_create_file(db_session, file_records["partitions"])
        event = ConfigurationEvent(
            mac_address=session.mac_address,
            operator=session.operator,
            configured_at=datetime.now(),
            configuration_action="SetupNewDevice",
            machine=machine_description(),
            repo_state=repo_state(),
            fanet_id=session.fanet_id,
            firmware=firmware_id,
            bootloader=bootloader_id,
            partitions=partitions_id,
            notes=session.notes or None,
            test_results=session.self_test_details,
        )
        db_session.add(event)
        db_session.commit()
        db_session.refresh(event)
        return event.id or 0


async def run_commissioning_session(
    session: CommissioningSession,
    *,
    sd_preparation: str = "format",
) -> None:
    try:
        mac_address = await read_session_mac_address(session)
        session.mac_address = mac_address
        session.tasks["network_discovery"]["details"] = (
            f"IP address: {session.device.ip_address}:{session.device.port}; "
            f"MAC address: {mac_address}"
        )

        if sd_preparation == "format":
            await prepare_session_sd_card(session)
        elif sd_preparation == "remount":
            await remount_session_sd_card(session)

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

        self_test_task = session.tasks["interactive_self_test"]
        self_test_task.update(
            {"status": "running", "details": "Starting verification tests..."}
        )
        session.touch()
        base_url = f"{device_base_url(session)}/self-test"
        await asyncio.to_thread(
            fetch_json,
            f"{base_url}/interactive",
            method="POST",
            timeout=SELF_TEST_HTTP_TIMEOUT_SECONDS,
        )
        last_device_response_at = time.monotonic()

        while True:
            try:
                self_test_payload = await asyncio.to_thread(
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
                self_test_task["details"] = (
                    "Waiting for device self-test response. The device may be busy "
                    "formatting the SD card.\n\n"
                    f"Last error: {type(poll_exc).__name__}: {poll_exc}\n"
                    f"Will keep waiting for {remaining} more seconds."
                )
                session.touch()
                await asyncio.sleep(SELF_TEST_POLL_SECONDS)
                continue

            session.self_test_result = self_test_payload
            self_test_task["result"] = self_test_payload
            self_test_task["details"] = json.dumps(self_test_payload, indent=2)
            session.touch()

            result = self_test_status(self_test_payload)
            if result is not None:
                self_test_task["status"] = result
                if result == "failure":
                    try:
                        self_test_task["details"] = await retrieve_session_self_test_details(
                            session, base_url
                        )
                    except Exception as details_exc:
                        self_test_task["details"] = (
                            "Verification tests failed.\n\n"
                            f"Test details could not be retrieved: "
                            f"{type(details_exc).__name__}: {details_exc}"
                        )
                    session.status = "failure"
                    session.details = "Verification tests failed."
                    return
                break

            if not self_test_payload.get("running", False):
                self_test_task["status"] = "failure"
                try:
                    self_test_task["details"] = await retrieve_session_self_test_details(
                        session, base_url
                    )
                except Exception as details_exc:
                    self_test_task["details"] = (
                        "Verification tests stopped without a pass/fail result.\n\n"
                        f"Test details could not be retrieved: "
                        f"{type(details_exc).__name__}: {details_exc}"
                    )
                session.status = "failure"
                session.details = "Verification tests stopped without a pass/fail result."
                return

            await asyncio.sleep(SELF_TEST_POLL_SECONDS)

        retrieve_test_details_task = session.tasks["retrieve_test_details"]
        retrieve_test_details_task.update(
            {"status": "running", "details": "Retrieving self test details..."}
        )
        session.touch()
        self_test_details = await retrieve_session_self_test_details(session, base_url)
        retrieve_test_details_task.update(
            {
                "status": "success",
                "details": self_test_details,
            }
        )
        session.touch()

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
        session.touch()

        persist_task = session.tasks["persist_results"]
        persist_task.update(
            {
                "status": "running",
                "details": "Writing commissioning log to database...",
            }
        )
        session.touch()
        event_id = await asyncio.to_thread(persist_configuration_event, session)
        session.configuration_event_id = event_id
        persist_task.update(
            {
                "status": "success",
                "details": f"Commissioning log written to database as event {event_id}.",
            }
        )

        clear_self_test_task = session.tasks["clear_self_test_results"]
        clear_self_test_task.update(
            {
                "status": "running",
                "details": "Clearing self test results from device SD card...",
            }
        )
        session.touch()
        clear_payload = await clear_session_self_test_results(session)
        deleted_count = int(clear_payload.get("deleted_count", 0))
        clear_self_test_task.update(
            {
                "status": "success",
                "details": f"Cleared {deleted_count} self test result file(s).",
                "deleted_count": deleted_count,
            }
        )

        notify_task = session.tasks["notify_commissioning_complete"]
        notify_task.update(
            {
                "status": "running",
                "details": "Notifying device that commissioning is complete...",
            }
        )
        session.touch()
        await notify_session_commissioning_complete(session)
        notify_task.update(
            {
                "status": "success",
                "details": "Device acknowledged successful commissioning.",
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


def retry_commissioning_format(session: CommissioningSession) -> CommissioningSession:
    if session.worker_task is not None:
        raise RuntimeError("Commissioning session is still running.")
    if session.tasks["prepare_sd_card"].get("status") != "failure":
        raise RuntimeError("SD card formatting has not failed.")
    result = session.tasks["prepare_sd_card"].get("result", {})
    if result.get("formatted", False):
        raise RuntimeError("The SD card is already formatted; retry mounting instead.")

    reset_session_tasks_from(session, "prepare_sd_card")
    session.status = "running"
    session.details = "Retrying SD card formatting."
    session.worker_task = asyncio.create_task(run_commissioning_session(session))
    session.touch()
    return session


def retry_commissioning_self_test(session: CommissioningSession) -> CommissioningSession:
    if session.worker_task is not None:
        raise RuntimeError("Commissioning session is still running.")
    if session.tasks["interactive_self_test"].get("status") != "failure":
        raise RuntimeError("Verification tests have not failed.")

    reset_session_tasks_from(session, "interactive_self_test")
    session.self_test_result = None
    session.self_test_details = None
    session.status = "running"
    session.details = "Restarting verification tests."
    session.worker_task = asyncio.create_task(
        run_commissioning_session(session, sd_preparation="skip")
    )
    session.touch()
    return session


def retry_commissioning_mount(session: CommissioningSession) -> CommissioningSession:
    validate_remount_recovery(session)
    reset_session_tasks_from(session, "prepare_sd_card")
    session.status = "running"
    session.details = "Retrying SD card mount."
    session.worker_task = asyncio.create_task(
        run_commissioning_session(session, sd_preparation="remount")
    )
    session.touch()
    return session


def restart_commissioning_device(session: CommissioningSession) -> CommissioningSession:
    validate_remount_recovery(session)
    reset_session_tasks_from(session, "prepare_sd_card")
    session.status = "running"
    session.details = "Restarting Leaf to recover the SD card."
    session.worker_task = asyncio.create_task(restart_and_resume_session(session))
    session.touch()
    return session


def validate_remount_recovery(session: CommissioningSession) -> None:
    if session.worker_task is not None:
        raise RuntimeError("Commissioning session is still running.")
    task = session.tasks["prepare_sd_card"]
    result = task.get("result", {})
    if task.get("status") != "failure" or not result.get("formatted", False):
        raise RuntimeError("The SD card does not have a recoverable remount failure.")
    if result.get("mounted", False):
        raise RuntimeError("The SD card is already mounted.")


def reset_session_tasks_from(session: CommissioningSession, task_name: str) -> None:
    task_order = [
        "prepare_sd_card",
        "firmware_version",
        "interactive_self_test",
        "retrieve_test_details",
        "fanet_id",
        "persist_results",
        "clear_self_test_results",
        "notify_commissioning_complete",
    ]
    start_index = task_order.index(task_name)
    for name in task_order[start_index:]:
        session.tasks[name] = idle_task()


def get_commissioning_session(mac_address: str) -> CommissioningSession | None:
    return commissioning_sessions.get(session_key(mac_address))


def list_commissioning_sessions() -> list[CommissioningSession]:
    return sorted(
        commissioning_sessions.values(),
        key=lambda session: session.created_at,
        reverse=True,
    )


def active_commissioning_device_identifiers() -> set[str]:
    identifiers: set[str] = set()
    for session in commissioning_sessions.values():
        if session.status != "running":
            continue
        for value in (
            session.mac_address,
            session.device.device_id,
            session.device.mac_address or "",
        ):
            identifiers.update(normalize_discovery_identifier(value))
    return identifiers


def active_commissioning_session_for_device(
    device: LeafDiscoveryResponse,
) -> CommissioningSession | None:
    device_identifiers = discovery_identifier_values(device)
    for session in commissioning_sessions.values():
        if session.status != "running":
            continue
        session_identifiers: set[str] = set()
        for value in (
            session.mac_address,
            session.device.device_id,
            session.device.mac_address or "",
        ):
            session_identifiers.update(normalize_discovery_identifier(value))
        if device_identifiers & session_identifiers:
            return session
    return None


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
