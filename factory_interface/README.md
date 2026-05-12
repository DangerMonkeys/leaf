# Factory Interface

A localhost-only web utility for helping a factory worker commission units.

## Prerequisites

- Python 3.12 or newer
- [uv](https://docs.astral.sh/uv/) installed and available on your `PATH`

## Start the Utility

From this directory:

```powershell
uv sync
uv run uvicorn factory_interface.app:app --host 127.0.0.1 --port 8000
```

Then open:

```text
http://127.0.0.1:8000
```

The app binds to `127.0.0.1` so it is only reachable from the local machine.

## Database Migrations

Alembic is configured for a local SQLite database at `leaf_devices.db`.

Create or update the database schema with:

```powershell
uv run alembic upgrade head
```

Create a new migration after changing SQLModel models with:

```powershell
uv run alembic revision --autogenerate -m "Describe change"
```
