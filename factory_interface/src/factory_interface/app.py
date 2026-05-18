from pathlib import Path
from urllib.parse import parse_qs

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse, Response
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from factory_interface.commissioning_tasks import (
    cancel_flash_task,
    get_flash_task,
    reset_flash_task,
    start_flash_firmware,
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
    cancel_self_test_task,
    get_self_test_task,
    reset_self_test_task,
    start_interactive_self_test,
)
from factory_interface.settings import (
    FactoryInterfaceSettings,
    find_firmware_paths,
    is_valid_firmware_path,
    load_settings,
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
        get_self_test_task(),
    ]
    if any(task.status == "running" for task in tasks):
        return
    if all(task.status == "idle" for task in tasks):
        return

    reset_flash_task()
    reset_find_device_task()
    reset_reset_nonvolatile_memory_task()
    reset_mac_address_task()
    reset_self_test_task()


def settings_template_context(
    request: Request,
    settings: FactoryInterfaceSettings,
    *,
    saved: bool,
) -> dict:
    firmware_paths = find_firmware_paths()
    return template_context(
        request,
        {
            "title": "Settings",
            "settings": settings,
            "saved": saved,
            "firmware_options": [
                {"name": path.name, "path": str(path)}
                for path in firmware_paths
            ],
            "selected_firmware_path": settings.firmware_path or "",
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


@app.post("/api/setup/cancel", response_class=JSONResponse)
async def cancel_setup_task() -> JSONResponse:
    cancel_flash_task()
    cancel_find_device_task()
    cancel_reset_nonvolatile_memory()
    cancel_mac_address_task()
    cancel_self_test_task()
    return JSONResponse(
        {
            "flash": get_flash_task().snapshot(),
            "network_discovery": get_find_device_task().snapshot(),
            "reset_nonvolatile_memory": get_reset_nonvolatile_memory_task().snapshot(),
            "mac_address": get_mac_address_task().snapshot(),
            "interactive_self_test": get_self_test_task().snapshot(),
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


@app.post("/api/setup/interactive-self-test", response_class=JSONResponse)
async def start_interactive_self_test_task() -> JSONResponse:
    task = start_interactive_self_test()
    return JSONResponse(task.snapshot())


@app.get("/api/setup/interactive-self-test", response_class=JSONResponse)
async def get_interactive_self_test_task() -> JSONResponse:
    task = get_self_test_task()
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
    firmware_path = form_data.get("firmware_path", [""])[0].strip() or None
    if not is_valid_firmware_path(firmware_path):
        firmware_paths = find_firmware_paths()
        firmware_path = str(firmware_paths[0]) if firmware_paths else None

    settings.esptool_path = esptool_path
    settings.firmware_path = firmware_path
    save_settings(settings)

    return templates.TemplateResponse(
        request,
        "settings.html",
        settings_template_context(request, settings, saved=True),
    )
