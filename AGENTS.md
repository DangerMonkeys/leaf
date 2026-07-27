# Agent Notes

## factory_interface setup tasks

- The `/setup` workflow does not use per-browser sessions. It is backed by process-wide module-level task singletons in `factory_interface/src/factory_interface/*_task.py` and `commissioning_tasks.py`. Multiple operators/browser tabs share the same setup task state.
- `/setup` calls `reset_setup_tasks_if_complete()` when the page loads. If any setup task is still `running`, state is preserved and the frontend rehydrates by polling the task GET endpoints. If no task is running and any task is complete/failed, the checklist is reset to idle on page load.
- The setup page auto-chains tasks in JavaScript: flash success starts discovery, discovery success starts nonvolatile reset, reset success starts MAC read, and MAC read success starts the interactive self-test. When changing terminal states, check that the frontend does not accidentally start the next task.
- The active post-discovery commissioning flow redirects from `/setup` to `/setup/{mac_address}` and runs the per-device session tasks there. Keep shared checklist rows in `setup_checklist.html` and `setup_session.html` visually in sync even when the `/setup` row is mostly a compatibility display.
- After the interactive self-test, the commissioning session runs a `retrieve_test_details` task. It calls the device webserver's `GET /self-test/details` endpoint, which streams the highest-numbered `/self_test_N.txt` file from the SD card as plain text, then stores that text in the session and persisted `test_results`.
- Cancellation is implemented by `POST /api/setup/cancel`. The backend cancels any currently running setup task and marks that task/checklist item as `failure`. A cancellation request when nothing is running is intentionally harmless.
- For cancellable tasks, storing the `asyncio.Task` worker handle matters. Otherwise a background coroutine can later overwrite a cancellation/failure state after racing with completion.
- Flash cancellation also tracks the subprocess handle so the running `esptool` process can be terminated/killed instead of only cancelling the Python coroutine.
- Firmware settings are intentionally split: application firmware can be either a local build (`local:<build path>`) or a GitHub release asset (`release:<tag>/firmware-*.bin`), while non-application binaries always come from a local PlatformIO build folder containing `bootloader.bin` and `partitions.bin`.
- GitHub release metadata is cached in `factory_interface/src/factory_interface/settings.json`; release assets are downloaded lazily during flashing into the gitignored `factory_interface/github_cache/` folder.

## Local verification

- For Leaf firmware work, use this verification workflow:
  1. Run the formatter:
     `powershell -ExecutionPolicy Bypass -File src\scripts\format_all_files.ps1`
  2. Build-test firmware with the `leaf_3_2_6_release` PlatformIO environment unless the user specifies a different hardware version/environment:
     `C:\Users\oxoth\.platformio\penv\Scripts\pio.exe run -e leaf_3_2_6_release`
  3. Auto-detect the connected ESP32/Leaf COM port instead of hardcoding one. Prefer exactly one port with Espressif VID `303A`, using:
     `C:\Users\oxoth\.platformio\penv\Scripts\python.exe -m serial.tools.list_ports -v`
  4. If exactly one likely ESP32/Leaf COM port is connected, attempt to flash the built firmware. It is acceptable for flashing to fail if the device is not connected or not in boot mode; report the esptool result clearly. Flash the app image to both OTA app slots so the currently selected boot partition does not matter. For the default v3.2.6 build, use:
     `C:\Users\oxoth\.platformio\penv\Scripts\python.exe C:\Users\oxoth\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 --port <AUTO_DETECTED_COM_PORT> --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode qio --flash_freq 80m --flash_size detect 0x10000 .pio\build\leaf_3_2_6_release\firmware.bin 0x340000 .pio\build\leaf_3_2_6_release\firmware.bin`
- `factory_interface` is a Python/uv project under `factory_interface/`; useful quick checks are:
  - `python -m compileall src\factory_interface`
  - `uv run python -c "from factory_interface.app import app; print(app.title)"`
- The app requires the operator cookie for API calls. Browser sessions may already have it, but direct API smoke tests need `factory_interface_operator_name=<name>`.
- Port `8000` may already be in use locally. Starting uvicorn on another port, such as `8001`, works for smoke testing.

## Core Dump Analysis

For ESP32 core dump retrieval and analysis:

**Reference**: [Leaf Core Dump Guide](https://leafvario.com/dev-references/core-dumps/)

**Firmware location**: `.pio/build/leaf_3_2_6_dev/firmware.elf`

**Device path (macOS)**: `/dev/tty.usbmodem*`

**Quick steps**:
```bash
cd esp-idf && . ./export.sh
espcoredump.py -p /dev/tty.usbmodem* info_corefile .pio/build/leaf_3_2_6_dev/firmware.elf
```
