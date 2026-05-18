# Agent Notes

## factory_interface setup tasks

- The `/setup` workflow does not use per-browser sessions. It is backed by process-wide module-level task singletons in `factory_interface/src/factory_interface/*_task.py` and `commissioning_tasks.py`. Multiple operators/browser tabs share the same setup task state.
- `/setup` calls `reset_setup_tasks_if_complete()` when the page loads. If any setup task is still `running`, state is preserved and the frontend rehydrates by polling the task GET endpoints. If no task is running and any task is complete/failed, the checklist is reset to idle on page load.
- The setup page auto-chains tasks in JavaScript: flash success starts discovery, discovery success starts nonvolatile reset, reset success starts MAC read, and MAC read success starts the interactive self-test. When changing terminal states, check that the frontend does not accidentally start the next task.
- Cancellation is implemented by `POST /api/setup/cancel`. The backend cancels any currently running setup task and marks that task/checklist item as `failure`. A cancellation request when nothing is running is intentionally harmless.
- For cancellable tasks, storing the `asyncio.Task` worker handle matters. Otherwise a background coroutine can later overwrite a cancellation/failure state after racing with completion.
- Flash cancellation also tracks the subprocess handle so the running `esptool` process can be terminated/killed instead of only cancelling the Python coroutine.
- Firmware settings are intentionally split: application firmware can be either a local build (`local:<build path>`) or a GitHub release asset (`release:<tag>/firmware-*.bin`), while non-application binaries always come from a local PlatformIO build folder containing `bootloader.bin` and `partitions.bin`.
- GitHub release metadata is cached in `factory_interface/src/factory_interface/settings.json`; release assets are downloaded lazily during flashing into the gitignored `factory_interface/github_cache/` folder.

## Local verification

- `factory_interface` is a Python/uv project under `factory_interface/`; useful quick checks are:
  - `python -m compileall src\factory_interface`
  - `uv run python -c "from factory_interface.app import app; print(app.title)"`
- The app requires the operator cookie for API calls. Browser sessions may already have it, but direct API smoke tests need `factory_interface_operator_name=<name>`.
- Port `8000` may already be in use locally. Starting uvicorn on another port, such as `8001`, works for smoke testing.
