from pathlib import Path
from urllib.parse import parse_qs

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

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
