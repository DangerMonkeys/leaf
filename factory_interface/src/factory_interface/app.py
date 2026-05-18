from pathlib import Path
from urllib.parse import parse_qs

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from factory_interface.commissioning_tasks import (
    get_flash_task,
    reset_flash_task,
    start_flash_firmware,
)
from factory_interface.mac_address_task import (
    get_mac_address_task,
    reset_mac_address_task,
    start_read_mac_address,
)
from factory_interface.network_discovery import (
    get_find_device_task,
    reset_find_device_task,
    start_find_device,
)
from factory_interface.nonvolatile_memory_task import (
    get_reset_nonvolatile_memory_task,
    reset_reset_nonvolatile_memory_task,
    start_reset_nonvolatile_memory,
)
from factory_interface.self_test_task import (
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
    settings: FactoryInterfaceSettings,
    *,
    saved: bool,
) -> dict:
    firmware_paths = find_firmware_paths()
    return {
        "title": "Settings",
        "settings": settings,
        "saved": saved,
        "firmware_options": [
            {"name": path.name, "path": str(path)}
            for path in firmware_paths
        ],
        "selected_firmware_path": settings.firmware_path or "",
    }


@app.get("/", response_class=HTMLResponse)
async def home(request: Request) -> HTMLResponse:
    return templates.TemplateResponse(
        request,
        "home.html",
        {"title": "Factory Interface"},
    )


@app.get("/setup", response_class=HTMLResponse)
async def setup_device(request: Request) -> HTMLResponse:
    reset_setup_tasks_if_complete()
    return templates.TemplateResponse(
        request,
        "setup_checklist.html",
        {"title": "Set up new device"},
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
        {"title": "Rework device"},
    )


@app.get("/settings", response_class=HTMLResponse)
async def settings(request: Request) -> HTMLResponse:
    settings = load_settings()
    return templates.TemplateResponse(
        request,
        "settings.html",
        settings_template_context(settings, saved=False),
    )


@app.post("/settings", response_class=HTMLResponse)
async def save_settings_page(request: Request) -> HTMLResponse:
    body = (await request.body()).decode()
    form_data = parse_qs(body, keep_blank_values=True)
    esptool_path = form_data.get("esptool_path", [""])[0].strip() or None
    firmware_path = form_data.get("firmware_path", [""])[0].strip() or None
    if not is_valid_firmware_path(firmware_path):
        firmware_paths = find_firmware_paths()
        firmware_path = str(firmware_paths[0]) if firmware_paths else None

    settings = FactoryInterfaceSettings(
        esptool_path=esptool_path,
        firmware_path=firmware_path,
    )
    save_settings(settings)

    return templates.TemplateResponse(
        request,
        "settings.html",
        settings_template_context(settings, saved=True),
    )
