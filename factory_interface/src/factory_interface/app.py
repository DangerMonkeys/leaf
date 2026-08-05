import asyncio
from pathlib import Path
from urllib.parse import parse_qs

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse, Response
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from factory_interface.commissioning_sessions import (
    active_commissioning_device_identifiers,
    active_commissioning_session_for_device,
    cancel_commissioning_session,
    create_commissioning_session,
    discard_commissioning_session,
    firmware_file_sources,
    get_commissioning_session,
    list_commissioning_sessions,
    manually_rediscover_session_device,
    restart_commissioning_device,
    retry_commissioning_format,
    retry_commissioning_mount,
    retry_commissioning_self_test,
)
from factory_interface.commissioning_tasks import (
    cancel_flash_task,
    get_flash_task,
    reset_flash_task,
    start_flash_firmware,
)
from factory_interface.firmware_version_task import (
    cancel_firmware_version_task,
    get_firmware_version_task,
    reset_firmware_version_task,
    start_read_firmware_version,
)
from factory_interface.fanet_id_task import (
    cancel_fanet_id_task,
    get_fanet_id_task,
    reset_fanet_id_task,
    start_assign_fanet_id,
)
from factory_interface.mac_address_task import (
    cancel_mac_address_task,
    get_mac_address_task,
    reset_mac_address_task,
    start_read_mac_address,
)
from factory_interface.network_discovery import (
    cancel_find_device_task,
    discovery_identifier_values,
    discovery_probe_targets,
    get_find_device_task,
    manually_select_discovered_device,
    preflash_discovery_snapshot,
    probe_once,
    reset_find_device_task,
    start_find_device,
)
from factory_interface.nonvolatile_memory_task import (
    cancel_reset_nonvolatile_memory,
    get_reset_nonvolatile_memory_task,
    reset_reset_nonvolatile_memory_task,
    start_reset_nonvolatile_memory,
)
from factory_interface.self_test_task import (
    cancel_self_test_details_task,
    cancel_self_test_task,
    get_self_test_details_task,
    get_self_test_task,
    reset_self_test_details_task,
    reset_self_test_task,
    start_interactive_self_test,
    start_retrieve_self_test_details,
)
from factory_interface.settings import (
    FactoryInterfaceSettings,
    application_firmware_options,
    describe_application_firmware_source,
    describe_non_application_binary_path,
    is_valid_application_firmware_source,
    is_valid_non_application_binary_path,
    load_settings,
    non_application_binary_options,
    refresh_github_releases,
    save_settings,
)
from factory_interface.pending_setup_runs import (
    PendingSetupRun,
    create_pending_setup_run,
    get_pending_setup_run,
    latest_running_pending_setup_run,
    list_pending_setup_runs,
    remove_pending_setup_run,
)
from factory_interface.serial_ports import available_serial_ports, eligible_serial_ports

PACKAGE_DIR = Path(__file__).resolve().parent

app = FastAPI(title="Factory Interface")
app.mount(
    "/static",
    StaticFiles(directory=PACKAGE_DIR / "static"),
    name="static",
)

templates = Jinja2Templates(directory=PACKAGE_DIR / "templates")


def static_asset_version(filename: str) -> int:
    return (PACKAGE_DIR / "static" / filename).stat().st_mtime_ns


templates.env.globals["static_asset_version"] = static_asset_version

OPERATOR_COOKIE_NAME = "factory_interface_operator_name"


def get_operator_name(request: Request) -> str | None:
    operator_name = request.cookies.get(OPERATOR_COOKIE_NAME, "").strip()
    return operator_name or None


def template_context(request: Request, context: dict | None = None) -> dict:
    page_context = dict(context or {})
    page_context["operator_name"] = get_operator_name(request)
    return page_context


def login_template_context(
    request: Request,
    *,
    operator_name: str = "",
    error: str | None = None,
) -> dict:
    return template_context(
        request,
        {
            "title": "Identify operator",
            "entered_operator_name": operator_name,
            "error": error,
        },
    )


@app.middleware("http")
async def require_operator_name(request: Request, call_next) -> Response:
    path = request.url.path
    is_public_path = path == "/login" or path.startswith("/static/")
    if is_public_path or get_operator_name(request) is not None:
        return await call_next(request)

    if path.startswith("/api/"):
        return JSONResponse(
            {"detail": "Operator name is required."},
            status_code=401,
        )

    return RedirectResponse(url="/login", status_code=303)


def reset_setup_tasks_if_complete() -> None:
    tasks = [
        get_flash_task(),
        get_find_device_task(),
        get_reset_nonvolatile_memory_task(),
        get_mac_address_task(),
        get_firmware_version_task(),
        get_fanet_id_task(),
        get_self_test_task(),
        get_self_test_details_task(),
    ]
    if any(task.status == "running" for task in tasks):
        return
    if all(task.status == "idle" for task in tasks):
        return

    reset_flash_task()
    reset_find_device_task()
    reset_reset_nonvolatile_memory_task()
    reset_mac_address_task()
    reset_firmware_version_task()
    reset_fanet_id_task()
    reset_self_test_task()
    reset_self_test_details_task()


def setup_preflight(settings: FactoryInterfaceSettings) -> dict:
    return {
        "application_firmware_label": describe_application_firmware_source(
            settings.application_firmware_source,
            settings,
        ),
        "non_application_binaries_label": describe_non_application_binary_path(
            settings.non_application_firmware_path,
        ),
        "firmware_files": firmware_file_sources(settings),
        "force_format_sd_card": settings.force_format_sd_card_during_commissioning,
        "notes": settings.setup_notes,
        "flash": get_flash_task().snapshot(),
    }


def commissioning_session_summaries() -> list[dict]:
    sessions = []
    for run in list_pending_setup_runs():
        sessions.append(run.snapshot())
    for session in list_commissioning_sessions():
        sessions.append(
            {
                "kind": "device",
                "label": session.mac_address,
                "url": f"/setup/{session.mac_address}",
                "status": session.status,
                "details": session.details,
                "created_at": session.created_at.isoformat(),
                "updated_at": session.updated_at.isoformat(),
            }
        )
    return sorted(sessions, key=lambda item: item["created_at"], reverse=True)


def promote_pending_setup_run(
    run: PendingSetupRun,
    device,
):
    mac_address = device.mac_address or device.device_id
    if not mac_address:
        raise RuntimeError("Discovery response did not include a MAC address.")

    existing_session = get_commissioning_session(mac_address)
    if existing_session is None:
        existing_session = active_commissioning_session_for_device(device)
    if existing_session is not None and existing_session.status == "running":
        remove_pending_setup_run(run.run_id)
        return existing_session

    preflight = dict(run.preflight)
    preflight["flash"] = get_flash_task().snapshot()
    session = create_commissioning_session(
        mac_address=mac_address,
        operator=run.operator,
        notes=run.notes,
        device=device,
        preflight=preflight,
    )
    remove_pending_setup_run(run.run_id)
    return session


async def monitor_pending_setup_run(run: PendingSetupRun) -> None:
    try:
        while get_pending_setup_run(run.run_id) is run:
            flash_task = get_flash_task()
            discovery_task = get_find_device_task()

            if discovery_task.status == "success" and discovery_task.device is not None:
                try:
                    promote_pending_setup_run(run, discovery_task.device)
                except Exception as exc:
                    run.status = "failure"
                    run.details = f"{type(exc).__name__}: {exc}"
                    run.touch()
                return

            if flash_task.status == "failure":
                run.status = "failure"
                run.details = "Firmware flashing failed."
                run.touch()
                return

            if discovery_task.status == "failure":
                run.status = "failure"
                run.details = discovery_task.error or "Network discovery failed."
                run.touch()
                return

            if flash_task.status == "running":
                details = "Flashing firmware."
            elif discovery_task.status == "running":
                details = "Searching for the newly flashed device."
            else:
                details = "Preparing commissioning."
            if run.details != details:
                run.details = details
                run.touch()
            await asyncio.sleep(0.25)
    except asyncio.CancelledError:
        return
    finally:
        if run.monitor_task is asyncio.current_task():
            run.monitor_task = None


def start_pending_setup_monitor(run: PendingSetupRun) -> None:
    if run.monitor_task is not None:
        run.monitor_task.cancel()
    run.monitor_task = asyncio.create_task(monitor_pending_setup_run(run))


def settings_template_context(
    request: Request,
    settings: FactoryInterfaceSettings,
    *,
    saved: bool,
) -> dict:
    firmware_options = application_firmware_options(settings)
    binary_options = non_application_binary_options()
    selected_application_firmware_details = next(
        (
            option["details"]
            for option in firmware_options
            if option["source"] == settings.application_firmware_source
        ),
        "",
    )
    return template_context(
        request,
        {
            "title": "Settings",
            "settings": settings,
            "saved": saved,
            "firmware_options": firmware_options,
            "binary_options": binary_options,
            "selected_application_firmware_source": (
                settings.application_firmware_source or ""
            ),
            "selected_application_firmware_details": (
                selected_application_firmware_details or "No firmware selected"
            ),
            "selected_non_application_firmware_path": (
                settings.non_application_firmware_path or ""
            ),
        },
    )


@app.get("/login", response_class=HTMLResponse)
async def login(request: Request) -> HTMLResponse:
    return templates.TemplateResponse(
        request,
        "login.html",
        login_template_context(
            request,
            operator_name=get_operator_name(request) or "",
        ),
    )


@app.post("/login", response_class=HTMLResponse)
async def save_login(request: Request) -> Response:
    body = (await request.body()).decode()
    form_data = parse_qs(body, keep_blank_values=True)
    operator_name = form_data.get("operator_name", [""])[0].strip()

    if not operator_name:
        return templates.TemplateResponse(
            request,
            "login.html",
            login_template_context(
                request,
                operator_name=operator_name,
                error="Enter an operator name.",
            ),
        )

    response = RedirectResponse(url="/", status_code=303)
    response.set_cookie(
        OPERATOR_COOKIE_NAME,
        operator_name,
        httponly=True,
        samesite="lax",
    )
    return response


@app.get("/", response_class=HTMLResponse)
async def setup_sessions(request: Request) -> HTMLResponse:
    return templates.TemplateResponse(
        request,
        "setup_sessions.html",
        template_context(
            request,
            {
                "title": "Commissioning sessions",
                "sessions": commissioning_session_summaries(),
            },
        ),
    )


@app.get("/setup", response_class=HTMLResponse)
async def setup_device(request: Request) -> HTMLResponse:
    run_id = request.query_params.get("run_id", "").strip()
    pending_run = get_pending_setup_run(run_id) if run_id else None
    if run_id and pending_run is None:
        return RedirectResponse(url="/", status_code=303)
    if pending_run is None:
        running_pending_run = latest_running_pending_setup_run()
        if running_pending_run is not None:
            return RedirectResponse(url=running_pending_run.snapshot()["url"], status_code=303)
        reset_setup_tasks_if_complete()
    settings = load_settings()
    return templates.TemplateResponse(
        request,
        "setup_checklist.html",
        template_context(
            request,
            {
                "title": "Set up new device",
                "setup_notes": settings.setup_notes,
                "application_firmware_label": describe_application_firmware_source(
                    settings.application_firmware_source,
                    settings,
                ),
                "non_application_binaries_label": describe_non_application_binary_path(
                    settings.non_application_firmware_path,
                ),
                "force_format_sd_card_during_commissioning": (
                    settings.force_format_sd_card_during_commissioning
                ),
                "serial_ports": available_serial_ports(),
                "pending_run_id": pending_run.run_id if pending_run else None,
            },
        ),
    )


@app.get("/discovery", response_class=HTMLResponse)
async def discovery_status(request: Request) -> HTMLResponse:
    discovery_task = get_find_device_task()
    active_session_identifiers = active_commissioning_device_identifiers()
    discovered_devices = []

    try:
        probe_responses = await probe_once()
        probe_error = None
    except OSError as exc:
        probe_responses = []
        probe_error = f"{type(exc).__name__}: {exc}"

    current_exclusions = set(discovery_task.excluded_device_ids)
    for response in probe_responses:
        identifiers = discovery_identifier_values(response)
        active_session = active_commissioning_session_for_device(response)
        reasons = []
        if identifiers & active_session_identifiers:
            reasons.append("matches a running commissioning session")
        if identifiers & current_exclusions:
            reasons.append("matches the current find-device exclusion set")
        if active_session is not None:
            reasons.append(f"running session: {active_session.mac_address}")

        discovered_devices.append(
            {
                "ip_address": response.ip_address,
                "port": response.port,
                "device_id": response.device_id,
                "mac_address": response.mac_address,
                "identifiers": sorted(identifiers),
                "candidate": not reasons,
                "reasons": reasons,
            }
        )

    return templates.TemplateResponse(
        request,
        "discovery.html",
        template_context(
            request,
            {
                "title": "Discovery status",
                "probe_error": probe_error,
                "probe_targets": discovery_probe_targets(),
                "devices": discovered_devices,
                "active_session_identifiers": sorted(active_session_identifiers),
                "find_device_task": discovery_task.snapshot(),
                "find_device_exclusions": sorted(discovery_task.excluded_device_ids),
                "preflash": preflash_discovery_snapshot(),
                "sessions": [
                    session.snapshot() for session in list_commissioning_sessions()
                ],
            },
        ),
    )


@app.get("/setup/{mac_address}", response_class=HTMLResponse)
async def setup_session(request: Request, mac_address: str) -> HTMLResponse:
    session = get_commissioning_session(mac_address)
    if session is None:
        return templates.TemplateResponse(
            request,
            "setup_session_missing.html",
            template_context(
                request,
                {
                    "title": "Commissioning session not found",
                    "mac_address": mac_address,
                },
            ),
            status_code=404,
        )

    return templates.TemplateResponse(
        request,
        "setup_session.html",
        template_context(
            request,
            {
                "title": f"Set up {session.mac_address}",
                "session": session.snapshot(),
            },
        ),
    )


@app.post("/api/setup/notes", response_class=JSONResponse)
async def save_setup_notes(request: Request) -> JSONResponse:
    payload = await request.json()
    notes = payload.get("notes", "")
    if not isinstance(notes, str):
        return JSONResponse(
            {"detail": "Notes must be text."},
            status_code=400,
        )

    settings = load_settings()
    settings.setup_notes = notes
    save_settings(settings)
    return JSONResponse({"setup_notes": settings.setup_notes})


@app.get("/api/setup/serial-ports", response_class=JSONResponse)
async def get_serial_ports() -> JSONResponse:
    return JSONResponse({"ports": available_serial_ports()})


@app.post("/api/setup/serial-ports/ignore", response_class=JSONResponse)
async def set_serial_port_ignored(request: Request) -> JSONResponse:
    payload = await request.json()
    device = payload.get("device")
    ignored = payload.get("ignored")
    if not isinstance(device, str) or not device.strip():
        return JSONResponse({"detail": "Serial port is required."}, status_code=400)
    if not isinstance(ignored, bool):
        return JSONResponse({"detail": "Ignored must be true or false."}, status_code=400)

    device = device.strip()
    settings = load_settings()
    ignored_ports = set(settings.ignored_serial_ports)
    if ignored:
        ignored_ports.add(device)
    else:
        ignored_ports.discard(device)
    settings.ignored_serial_ports = sorted(ignored_ports, key=str.lower)
    save_settings(settings)
    return JSONResponse({"ports": available_serial_ports()})


@app.post("/api/setup/handoff", response_class=JSONResponse)
async def handoff_setup_session(request: Request) -> JSONResponse:
    discovery_task = get_find_device_task()
    if discovery_task.status != "success" or discovery_task.device is None:
        return JSONResponse(
            {"detail": "Device has not been discovered on the network."},
            status_code=409,
        )

    device = discovery_task.device
    mac_address = device.mac_address or device.device_id
    if not mac_address:
        return JSONResponse(
            {"detail": "Discovery response did not include a MAC address."},
            status_code=409,
        )
    existing_session = get_commissioning_session(mac_address)
    if existing_session is not None and existing_session.status == "running":
        return JSONResponse(
            {
                "session": existing_session.snapshot(),
                "url": f"/setup/{existing_session.mac_address}",
            }
        )
    active_session = active_commissioning_session_for_device(device)
    if active_session is not None:
        return JSONResponse(
            {
                "session": active_session.snapshot(),
                "url": f"/setup/{active_session.mac_address}",
            }
        )

    run_id = request.query_params.get("run_id", "").strip()
    pending_run = get_pending_setup_run(run_id) if run_id else None
    if pending_run is None:
        pending_run = latest_running_pending_setup_run()

    if pending_run is not None:
        session = promote_pending_setup_run(pending_run, device)
    else:
        operator_name = get_operator_name(request) or "unknown"
        settings = load_settings()
        session = create_commissioning_session(
            mac_address=mac_address,
            operator=operator_name,
            notes=settings.setup_notes,
            device=device,
            preflight=setup_preflight(settings),
        )
    return JSONResponse(
        {
            "session": session.snapshot(),
            "url": f"/setup/{session.mac_address}",
        }
    )


@app.get("/api/setup/sessions", response_class=JSONResponse)
async def get_setup_sessions() -> JSONResponse:
    return JSONResponse(
        {"sessions": [session.snapshot() for session in list_commissioning_sessions()]}
    )


@app.get("/api/setup/session-summaries", response_class=JSONResponse)
async def get_setup_session_summaries() -> JSONResponse:
    return JSONResponse({"sessions": commissioning_session_summaries()})


@app.get("/api/setup/sessions/{mac_address}", response_class=JSONResponse)
async def get_setup_session_status(mac_address: str) -> JSONResponse:
    session = get_commissioning_session(mac_address)
    if session is None:
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)
    return JSONResponse(session.snapshot())


@app.post("/api/setup/sessions/{mac_address}/network-discovery/manual", response_class=JSONResponse)
async def manually_select_session_network_device(
    mac_address: str,
    request: Request,
) -> JSONResponse:
    session = get_commissioning_session(mac_address)
    if session is None:
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)

    payload = await request.json()
    ip_address = str(payload.get("ip_address", "")).strip()
    if not ip_address:
        return JSONResponse({"detail": "Device IP address is required."}, status_code=400)

    try:
        await manually_rediscover_session_device(session, ip_address)
    except ValueError as exc:
        return JSONResponse({"detail": str(exc)}, status_code=400)
    except RuntimeError as exc:
        return JSONResponse({"detail": str(exc)}, status_code=404)
    except OSError as exc:
        return JSONResponse({"detail": f"{type(exc).__name__}: {exc}"}, status_code=502)

    return JSONResponse(session.snapshot())


@app.post("/api/setup/sessions/{mac_address}/cancel", response_class=JSONResponse)
async def cancel_setup_session(mac_address: str) -> JSONResponse:
    session = cancel_commissioning_session(mac_address)
    if session is None:
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)
    return JSONResponse(session.snapshot())


@app.post(
    "/api/setup/sessions/{mac_address}/retry-format",
    response_class=JSONResponse,
)
async def retry_setup_session_format(mac_address: str) -> JSONResponse:
    session = get_commissioning_session(mac_address)
    if session is None:
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)
    try:
        retry_commissioning_format(session)
    except RuntimeError as exc:
        return JSONResponse({"detail": str(exc)}, status_code=409)
    return JSONResponse(session.snapshot())


@app.post(
    "/api/setup/sessions/{mac_address}/retry-mount",
    response_class=JSONResponse,
)
async def retry_setup_session_mount(mac_address: str) -> JSONResponse:
    session = get_commissioning_session(mac_address)
    if session is None:
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)
    try:
        retry_commissioning_mount(session)
    except RuntimeError as exc:
        return JSONResponse({"detail": str(exc)}, status_code=409)
    return JSONResponse(session.snapshot())


@app.post(
    "/api/setup/sessions/{mac_address}/restart-device",
    response_class=JSONResponse,
)
async def restart_setup_session_device(mac_address: str) -> JSONResponse:
    session = get_commissioning_session(mac_address)
    if session is None:
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)
    try:
        restart_commissioning_device(session)
    except RuntimeError as exc:
        return JSONResponse({"detail": str(exc)}, status_code=409)
    return JSONResponse(session.snapshot())


@app.post(
    "/api/setup/sessions/{mac_address}/retry-self-test",
    response_class=JSONResponse,
)
async def retry_setup_session_self_test(mac_address: str) -> JSONResponse:
    session = get_commissioning_session(mac_address)
    if session is None:
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)
    try:
        retry_commissioning_self_test(session)
    except RuntimeError as exc:
        return JSONResponse({"detail": str(exc)}, status_code=409)
    return JSONResponse(session.snapshot())


@app.delete("/api/setup/sessions/{mac_address}", response_class=JSONResponse)
async def discard_setup_session(mac_address: str) -> JSONResponse:
    if not discard_commissioning_session(mac_address):
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)
    return JSONResponse({"discarded": True})


@app.post("/api/setup/cancel", response_class=JSONResponse)
async def cancel_setup_task() -> JSONResponse:
    cancel_flash_task()
    cancel_find_device_task()
    cancel_reset_nonvolatile_memory()
    cancel_mac_address_task()
    cancel_firmware_version_task()
    cancel_fanet_id_task()
    cancel_self_test_task()
    cancel_self_test_details_task()
    return JSONResponse(
        {
            "flash": get_flash_task().snapshot(),
            "network_discovery": get_find_device_task().snapshot(),
            "reset_nonvolatile_memory": get_reset_nonvolatile_memory_task().snapshot(),
            "mac_address": get_mac_address_task().snapshot(),
            "firmware_version": get_firmware_version_task().snapshot(),
            "fanet_id": get_fanet_id_task().snapshot(),
            "interactive_self_test": get_self_test_task().snapshot(),
            "retrieve_test_details": get_self_test_details_task().snapshot(),
        }
    )


@app.post("/api/setup/flash", response_class=JSONResponse)
async def start_flash_firmware_task(request: Request) -> JSONResponse:
    run_id = request.query_params.get("run_id", "").strip()
    pending_run = get_pending_setup_run(run_id) if run_id else None
    if pending_run is None:
        pending_run = latest_running_pending_setup_run()
    pending_run_was_running = pending_run is not None and pending_run.status == "running"

    settings = load_settings()
    if pending_run is None:
        pending_run = create_pending_setup_run(
            operator=get_operator_name(request) or "unknown",
            notes=settings.setup_notes,
            preflight=setup_preflight(settings),
            serial_ports=eligible_serial_ports(),
        )
    elif pending_run.status != "running":
        pending_run.status = "running"
        pending_run.details = "Preparing to flash firmware."
        pending_run.preflight = setup_preflight(settings)
        pending_run.serial_ports = eligible_serial_ports()
        pending_run.touch()

    flash_task = get_flash_task()
    discovery_task = get_find_device_task()
    setup_already_running = pending_run_was_running and (
        flash_task.status == "running"
        or discovery_task.status in {"running", "success"}
    )
    if not setup_already_running:
        flash_task = start_flash_firmware()
    start_pending_setup_monitor(pending_run)

    payload = flash_task.snapshot()
    payload["pending_run_id"] = pending_run.run_id
    return JSONResponse(payload)


@app.get("/api/setup/flash", response_class=JSONResponse)
async def get_flash_firmware_task() -> JSONResponse:
    task = get_flash_task()
    return JSONResponse(task.snapshot())


@app.post("/api/setup/network-discovery", response_class=JSONResponse)
async def start_network_discovery_task() -> JSONResponse:
    task = start_find_device(
        excluded_device_ids=active_commissioning_device_identifiers()
    )
    return JSONResponse(task.snapshot())


@app.post("/api/setup/network-discovery/manual", response_class=JSONResponse)
async def manually_select_network_device(request: Request) -> JSONResponse:
    payload = await request.json()
    ip_address = str(payload.get("ip_address", "")).strip()
    if not ip_address:
        return JSONResponse({"detail": "Device IP address is required."}, status_code=400)

    try:
        response = await manually_select_discovered_device(ip_address)
    except ValueError as exc:
        return JSONResponse({"detail": str(exc)}, status_code=400)
    except RuntimeError as exc:
        return JSONResponse({"detail": str(exc)}, status_code=404)
    except OSError as exc:
        return JSONResponse({"detail": f"{type(exc).__name__}: {exc}"}, status_code=502)

    if active_commissioning_session_for_device(response) is not None:
        reset_find_device_task()
        return JSONResponse(
            {"detail": "Discovered device already has an active commissioning session."},
            status_code=409,
        )

    return JSONResponse(get_find_device_task().snapshot())


@app.get("/api/setup/network-discovery", response_class=JSONResponse)
async def get_network_discovery_task() -> JSONResponse:
    task = get_find_device_task()
    return JSONResponse(task.snapshot())


@app.post("/api/setup/reset-nonvolatile-memory", response_class=JSONResponse)
async def start_reset_nonvolatile_memory_task() -> JSONResponse:
    task = start_reset_nonvolatile_memory()
    return JSONResponse(task.snapshot())


@app.get("/api/setup/reset-nonvolatile-memory", response_class=JSONResponse)
async def get_reset_nonvolatile_memory_task_status() -> JSONResponse:
    task = get_reset_nonvolatile_memory_task()
    return JSONResponse(task.snapshot())


@app.post("/api/setup/mac-address", response_class=JSONResponse)
async def start_mac_address_task() -> JSONResponse:
    task = start_read_mac_address()
    return JSONResponse(task.snapshot())


@app.get("/api/setup/mac-address", response_class=JSONResponse)
async def get_mac_address_task_status() -> JSONResponse:
    task = get_mac_address_task()
    return JSONResponse(task.snapshot())


@app.post("/api/setup/firmware-version", response_class=JSONResponse)
async def start_firmware_version_task() -> JSONResponse:
    task = start_read_firmware_version()
    return JSONResponse(task.snapshot())


@app.get("/api/setup/firmware-version", response_class=JSONResponse)
async def get_firmware_version_task_status() -> JSONResponse:
    task = get_firmware_version_task()
    return JSONResponse(task.snapshot())


@app.post("/api/setup/fanet-id", response_class=JSONResponse)
async def start_fanet_id_task() -> JSONResponse:
    task = start_assign_fanet_id()
    return JSONResponse(task.snapshot())


@app.get("/api/setup/fanet-id", response_class=JSONResponse)
async def get_fanet_id_task_status() -> JSONResponse:
    task = get_fanet_id_task()
    return JSONResponse(task.snapshot())


@app.post("/api/setup/interactive-self-test", response_class=JSONResponse)
async def start_interactive_self_test_task() -> JSONResponse:
    task = start_interactive_self_test()
    return JSONResponse(task.snapshot())


@app.get("/api/setup/interactive-self-test", response_class=JSONResponse)
async def get_interactive_self_test_task() -> JSONResponse:
    task = get_self_test_task()
    return JSONResponse(task.snapshot())


@app.post("/api/setup/test-details", response_class=JSONResponse)
async def start_retrieve_self_test_details_task() -> JSONResponse:
    task = start_retrieve_self_test_details()
    return JSONResponse(task.snapshot())


@app.get("/api/setup/test-details", response_class=JSONResponse)
async def get_retrieve_self_test_details_task() -> JSONResponse:
    task = get_self_test_details_task()
    return JSONResponse(task.snapshot())


@app.get("/settings", response_class=HTMLResponse)
async def settings(request: Request) -> HTMLResponse:
    settings = load_settings()
    return templates.TemplateResponse(
        request,
        "settings.html",
        settings_template_context(request, settings, saved=False),
    )


@app.post("/settings", response_class=HTMLResponse)
async def save_settings_page(request: Request) -> HTMLResponse:
    settings = load_settings()
    body = (await request.body()).decode()
    form_data = parse_qs(body, keep_blank_values=True)
    esptool_path = form_data.get("esptool_path", [""])[0].strip() or None
    application_firmware_source = (
        form_data.get("application_firmware_source", [""])[0].strip() or None
    )
    non_application_firmware_path = (
        form_data.get("non_application_firmware_path", [""])[0].strip() or None
    )
    force_format_sd_card = (
        form_data.get("force_format_sd_card_during_commissioning", [""])[0] == "true"
    )
    if not is_valid_application_firmware_source(application_firmware_source, settings):
        firmware_options = application_firmware_options(settings)
        application_firmware_source = firmware_options[0]["source"] if firmware_options else None
    if not is_valid_non_application_binary_path(non_application_firmware_path):
        binary_options = non_application_binary_options()
        non_application_firmware_path = binary_options[0]["path"] if binary_options else None

    settings.esptool_path = esptool_path
    settings.application_firmware_source = application_firmware_source
    settings.non_application_firmware_path = non_application_firmware_path
    settings.force_format_sd_card_during_commissioning = force_format_sd_card
    settings.firmware_path = non_application_firmware_path
    save_settings(settings)

    return templates.TemplateResponse(
        request,
        "settings.html",
        settings_template_context(request, settings, saved=True),
    )


@app.post("/api/settings/github-releases/refresh", response_class=JSONResponse)
async def refresh_github_release_options() -> JSONResponse:
    settings = load_settings()
    try:
        refresh_github_releases(settings)
    except RuntimeError as exc:
        return JSONResponse({"detail": str(exc)}, status_code=502)

    return JSONResponse(
        {
            "firmware_options": application_firmware_options(settings),
            "selected_application_firmware_source": (
                settings.application_firmware_source or ""
            ),
        }
    )
