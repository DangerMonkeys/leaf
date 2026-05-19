from pathlib import Path
from urllib.parse import parse_qs

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse, Response
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from factory_interface.commissioning_sessions import (
    cancel_commissioning_session,
    create_commissioning_session,
    discard_commissioning_session,
    get_commissioning_session,
    list_commissioning_sessions,
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
    get_find_device_task,
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

PACKAGE_DIR = Path(__file__).resolve().parent

app = FastAPI(title="Factory Interface")
app.mount(
    "/static",
    StaticFiles(directory=PACKAGE_DIR / "static"),
    name="static",
)

templates = Jinja2Templates(directory=PACKAGE_DIR / "templates")

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
async def home(request: Request) -> HTMLResponse:
    return templates.TemplateResponse(
        request,
        "home.html",
        template_context(request, {"title": "Factory Interface"}),
    )


@app.get("/setup", response_class=HTMLResponse)
async def setup_device(request: Request) -> HTMLResponse:
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
            },
        ),
    )


@app.get("/setup/sessions", response_class=HTMLResponse)
async def setup_sessions(request: Request) -> HTMLResponse:
    return templates.TemplateResponse(
        request,
        "setup_sessions.html",
        template_context(
            request,
            {
                "title": "Commissioning sessions",
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

    operator_name = get_operator_name(request) or "unknown"
    settings = load_settings()
    preflight = {
        "application_firmware_label": describe_application_firmware_source(
            settings.application_firmware_source,
            settings,
        ),
        "non_application_binaries_label": describe_non_application_binary_path(
            settings.non_application_firmware_path,
        ),
        "notes": settings.setup_notes,
        "flash": get_flash_task().snapshot(),
    }
    session = create_commissioning_session(
        mac_address=mac_address,
        operator=operator_name,
        notes=settings.setup_notes,
        device=device,
        preflight=preflight,
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


@app.get("/api/setup/sessions/{mac_address}", response_class=JSONResponse)
async def get_setup_session_status(mac_address: str) -> JSONResponse:
    session = get_commissioning_session(mac_address)
    if session is None:
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)
    return JSONResponse(session.snapshot())


@app.post("/api/setup/sessions/{mac_address}/cancel", response_class=JSONResponse)
async def cancel_setup_session(mac_address: str) -> JSONResponse:
    session = cancel_commissioning_session(mac_address)
    if session is None:
        return JSONResponse({"detail": "Commissioning session not found."}, status_code=404)
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
async def start_flash_firmware_task() -> JSONResponse:
    task = start_flash_firmware()
    return JSONResponse(task.snapshot())


@app.get("/api/setup/flash", response_class=JSONResponse)
async def get_flash_firmware_task() -> JSONResponse:
    task = get_flash_task()
    return JSONResponse(task.snapshot())


@app.post("/api/setup/network-discovery", response_class=JSONResponse)
async def start_network_discovery_task() -> JSONResponse:
    task = start_find_device()
    return JSONResponse(task.snapshot())


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


@app.get("/rework", response_class=HTMLResponse)
async def rework_device(request: Request) -> HTMLResponse:
    return templates.TemplateResponse(
        request,
        "rework_device.html",
        template_context(request, {"title": "Rework device"}),
    )


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
    if not is_valid_application_firmware_source(application_firmware_source, settings):
        firmware_options = application_firmware_options(settings)
        application_firmware_source = firmware_options[0]["source"] if firmware_options else None
    if not is_valid_non_application_binary_path(non_application_firmware_path):
        binary_options = non_application_binary_options()
        non_application_firmware_path = binary_options[0]["path"] if binary_options else None

    settings.esptool_path = esptool_path
    settings.application_firmware_source = application_firmware_source
    settings.non_application_firmware_path = non_application_firmware_path
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
