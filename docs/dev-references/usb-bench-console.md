---
title: Leaf USB bench console
description: Drive a test unit over USB and capture comparable on-device heap measurements.
---

## Build a candidate for the bench

Check out the candidate commit and run PlatformIO from the repository root.

```powershell
pio run -e leaf_3_2_5_bench
```

The 3.2.5 bench target inherits `leaf_3_2_5_release` and adds
`LEAF_USB_BENCH_CONSOLE=1`. For another hardware revision, define a matching
`*_bench` environment in the variant's `platformio.ini` using
`leaf_3_2_5_bench` as the template. Build one environment at a time per worktree
because the version script temporarily updates a shared header.

Flash `.pio/build/leaf_3_2_5_bench/firmware.bin` to both OTA app slots using the
repository's hardware verification procedure for the connected unit. The build
also copies the image to `bench-firmware-leaf_3_2_5.bin` for local use. Archive
the tested binary, ELF, commit ID, and report together. Release builds leave
the console flag off. For publication, use the release image and follow the
runtime assessment and release checklist. [Creating a firmware release](releasing.md)

The operating-mode SD handoff revokes USB host access to the SD card and waits
for pending host I/O. The bench build then reconnects USB so the console remains
available. The COM port can disappear during that transition. Reopen it after
it re-enumerates. Use the release image to verify production USB and power
behavior.

## Drive the unit

With a USB data cable, run these commands from the repository root using a
Python environment that has pySerial installed. The script selects a single
Leaf application port automatically. Put `--port COM19` before the subcommand
to specify a port explicitly.

```powershell
python utils/leaf_bench_console.py send version status config heap ip web
python utils/leaf_bench_console.py send "press DOWN" "press UP" status
python utils/leaf_bench_console.py send "hold CENTER 1000"
```

Buttons are `UP`, `DOWN`, `LEFT`, `RIGHT`, and `CENTER`. A press lasts 80 ms.
A hold accepts 850–5000 ms. Firmware handles the synthetic input through the
normal debounce and hold logic, and physical input takes precedence. A button
reply acknowledges that the action was accepted. Use `status` to check the
result. The console cannot make a fully unpowered unit boot.

`status` reports power state, page, the main-menu or top modal cursor, modal depth, warning,
vario readiness, GPS fix, flight timer, and firmware access to the SD card.
`ip` reports the station and access point IPv4 addresses. `web` gives the URL
when the Web App is active. Open the Web App through its normal menu. For HTTP
tests, connect the computer to the same network or to Leaf's access point.
`wifi` reports the active radio mode such as 0 for off, 1 for station, or 2 for
access point. `routeheap` reports the start and minimum free internal heap
for route body receipt and editor normalization, in that order. Zero means a
phase did not record a measurement. `fanet` reports module detection, radio state, and
the TX and RX worker stack high-water marks in bytes. The stack value is
zero when the worker is absent. The `navheap` reply gives start and minimum
free internal heap for route import and saved-waypoint listing in that order.
These are phase readings from the most recent run of each operation.
`buslog` reports whether the logger has an open file, then the saved
startup-logging flag. The old Vario CSV field in `config` is reserved as zero
so older bench clients can read the same reply format.

## Capture and compare heap

Use a cold boot and let the vario finish warming up on a primary page, with
the flight timer stopped. Keep radio, network, SD and diagnostics settings
consistent between runs. Record normal on-state heap with the Web App closed
first. A second run can measure Web App idle as a separate workload.

Pass the exact `.bin` that was flashed and choose a fresh output path. The
capture checks for the reported version string in that file and records the
file's SHA-256. Verify the flash itself when establishing the image identity.

```powershell
python utils/leaf_bench_console.py baseline --firmware .pio/build/leaf_3_2_5_bench/firmware.bin --output .pio/bench-baseline.json
```

Compare the idle median and range, the boot-to-date minimum, and the largest
free block on the same unit with the same build flag, startup sequence, settings,
and sampling interval. Keep the console idle between samples. The loop task
stack high-water mark is the smallest unused stack space observed since boot.
The capture reports all four metrics in bytes.

For the paired Web App workload, run the following on the warmed-up User
screen with Developer mode on. The host needs a network path to the Leaf
when the Web App opens. The script reads two screenshots, submits a route
request that fails validation without saving, and measures heap through
Web App closure.

```powershell
python utils/leaf_bench_web_memory.py --firmware .pio/build/leaf_3_2_5_bench/firmware.bin --output .pio/web-memory.json
```

See the [firmware heap savings log](heap-savings-log.md) for the paired
baseline and candidate results.

The route regression saves two inactive, disposable files under `/routes`.
Choose an unused prefix on the SD card. After the run, inspect and remove the
two files through USB mass storage in charging mode.

```powershell
python utils/leaf_bench_route_requests.py --firmware .pio/build/leaf_3_2_5_bench/firmware.bin --prefix bench-my-run --output .pio/route-test.json
python utils/leaf_bench_wifi_idle.py --firmware .pio/build/leaf_3_2_5_bench/firmware.bin --output .pio/wifi-test.json
```

## Initial 3.2.5 on-state baseline

The November 13, 2026 capture (November 14 at 01:47 UTC) sampled a USB-connected
Leaf 3.2.5 on the User page 20 times over 19.047 seconds. The vario was ready,
the flight timer was stopped, the GPS had no fix, and the firmware could access
the SD card. The Web App was closed. Bluetooth was configured on, FANET region
was set to 1, and Developer mode was on with the captured diagnostic logs and
CPU capture disabled. The raw samples are in
`utils/bench_baselines/leaf-3.2.5-idle-on.json` in the repository.

| Metric | Bytes in every sample |
| --- | ---: |
| Free internal heap | 40,296 |
| Minimum free internal heap since boot | 30,560 |
| Largest free internal block | 31,732 |
| Loop task stack high-water mark | 7,848 |

Both OTA slots were flashed and verified. The console woke the unit from
charging mode, reconnected after the SD handoff, and navigated to the Web App.
From the host, `GET /app` and `GET /api/user/status` returned HTTP 200.
The Web App was then closed for the heap capture.

- Build: `leaf_3_2_5_bench`, firmware `0.2.4-03ed.bench+h3.2.5`.
- Source: the bench implementation was built as local changes on
  `03ead991e92ff7907f331ade535cc05061b841be` before it was committed as
  `60496019`. Use the recorded binary hash to identify the exact tested build.
- Firmware SHA-256: `46705b76c2b2e4d1557c24aec42a2eac5777c20422ebba4298573d45ce5c3b6f`.
- Toolchain: PlatformIO Core 6.2.0, pioarduino 53.03.10, Arduino ESP32 3.1.0,
  and Xtensa GCC 13.2.0+20240530.
