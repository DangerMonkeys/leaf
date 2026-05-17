from pathlib import Path
from urllib.parse import parse_qs

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from factory_interface.commissioning_tasks import get_flash_task, start_flash_firmware
from factory_interface.network_discovery import get_find_device_task, start_find_device
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
    return templates.TemplateResponse(
        request,
        "setup_device.html",
        {"title": "Set up new device"},
    )


@app.get("/setup/{serial_number}", response_class=HTMLResponse)
async def setup_device_checklist(request: Request, serial_number: str) -> HTMLResponse:
    return templates.TemplateResponse(
        request,
        "setup_checklist.html",
        {"title": f"Set up {serial_number}", "serial_number": serial_number},
    )


@app.post("/api/setup/{serial_number}/flash", response_class=JSONResponse)
async def start_flash_firmware_task(serial_number: str) -> JSONResponse:
    task = start_flash_firmware(serial_number)
    return JSONResponse(task.snapshot())


@app.get("/api/setup/{serial_number}/flash", response_class=JSONResponse)
async def get_flash_firmware_task(serial_number: str) -> JSONResponse:
    task = get_flash_task(serial_number)
    return JSONResponse(task.snapshot())


@app.post("/api/setup/{serial_number}/network-discovery", response_class=JSONResponse)
async def start_network_discovery_task(serial_number: str) -> JSONResponse:
    task = start_find_device(serial_number)
    return JSONResponse(task.snapshot())


@app.get("/api/setup/{serial_number}/network-discovery", response_class=JSONResponse)
async def get_network_discovery_task(serial_number: str) -> JSONResponse:
    task = get_find_device_task(serial_number)
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
