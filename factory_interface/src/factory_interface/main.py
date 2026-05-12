import uvicorn


def run() -> None:
    uvicorn.run(
        "factory_interface.app:app",
        host="127.0.0.1",
        port=8000,
        reload=True,
    )
