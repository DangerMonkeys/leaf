from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

PACKAGE_DIR = Path(__file__).resolve().parent

app = FastAPI(title="Factory Interface")
app.mount(
    "/static",
    StaticFiles(directory=PACKAGE_DIR / "static"),
    name="static",
)

templates = Jinja2Templates(directory=PACKAGE_DIR / "templates")


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
    return templates.TemplateResponse(
        request,
        "settings.html",
        {"title": "Settings"},
    )
