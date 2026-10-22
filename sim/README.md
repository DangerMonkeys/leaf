# leafsim — the Leaf device emulator

`leafsim` runs the **real Leaf firmware** — the same `src/vario` sources that get flashed to a
device — on your machine, against a virtual ESP32 board, a virtual clock, and sensor data played
from a recording. You get the device's screen in a browser, working buttons, and a way to fly a
flight through it without leaving the ground.

It is not a mock of the app. The display you see is drawn by the firmware's own u8g2 code, the
menus are the firmware's menu tree, the vario beeps are the frequencies the firmware wrote to the
speaker channel, and the SD card is a folder you can open afterwards to read the IGC file it
logged.

```powershell
.\sim\run.ps1                                       # control panel on http://localhost:8080
.\sim\run.ps1 -Scenario sim/recordings/thermal-climb.json
```

```sh
./sim/run.sh --scenario sim/recordings/example.igc
```

Both runners pass anything they do not recognise straight to `leafsim`, so every invocation
below works through them (after `--` for `run.sh`):

```sh
./sim/run.sh -- --setting THERM_DISPLAY=1 --setting SHOW_THERM_TRK=1
```

The first build takes a few minutes; later ones are incremental. Nothing is installed on your
machine — everything runs in a `gcc:13` container with the repository bind-mounted.

The emulated device boots like a real one: it sits on the splash screen until the barometer and
IMU have delivered their startup samples (so load a scenario and press play, or it waits forever,
exactly as a device with dead sensors would), and then shows the safety disclaimer. Press **DOWN**
to move the cursor onto ACCEPT, then **CENTER** — the same two presses the hardware needs.

**Prerequisites**: Docker, and one PlatformIO firmware build having been run at some point
(`pio run -e leaf_3_2_7_release`) so that `.pio/libdeps` holds ETL, ArduinoJson, IgcLogger and the
FANET headers. The emulator uses the same library versions as the firmware.

---

## The control panel

| | |
|---|---|
| **Screen** | The 96×192 panel, 3× scale, redrawn as the firmware redraws it |
| **Buttons** | Click them, or use the arrow keys and enter. Press-and-hold works, so hold-to-power-off and hold-to-repeat behave as they do in your hand |
| **Clock** | ¼× to "max", pause, and single-step by 100 ms or 1 s of device time |
| **Scenario** | Pick a recording, play/pause, scrub |
| **Flight path** | The loaded recording in 3D — drag to orbit, wheel to zoom, shift-drag to pan. The track is coloured by climb rate and dimmed ahead of the playhead, with drop lines to a ground grid so height reads as distance |
| **Instruments** | Altitude, climb, pressure, GPS fix, ground speed, flight-timer state, current speaker tone |
| **Board** | Battery percentage, charging state, and card insert/eject — all of which the firmware reacts to |
| **Serial console** | The device's debug output, including fatal errors |
| **Speaker** | Tick the box and the browser plays the vario tones the firmware is generating |

## Recordings

### Live free flight

Choose **Free flight · live thermal** in the Scenario card and click **Load**. The training-style
map appears beside the real firmware LCD. Left/right arrows adjust persistent turn rate,
up/down arrows adjust airspeed, **C** straightens, and **Space** pauses/resumes the shared clock.
Leaf's buttons remain clickable. Steering buttons also work with a mouse or touch. Held input
is released on focus loss, and the server expires unrefreshed input after 750 ms.

The thermal is a fixed-source, tilted plume by default. Show/hide its cross-section at the
glider's current altitude without changing the flight physics. Controls adjust diameter, peak
upward air speed, peakiness, wind speed/direction, tilt, variation with height, texture and glider
sink. Higher peakiness produces a more concentrated core. The optional drifting-column mode is
useful for comparing assumptions. This is a simplified flight model: sink is independent of
airspeed/bank and the terrain is graphical, with no collision or terrain-lift model.

The emulator owns flight physics and time, synthesizing pressure and motion at 20 Hz and GPS
at 1 Hz through the recording player's sensor encoder. The firmware estimates its own wind and
lift; no thermal truth or browser-side estimate is supplied to it. Restart flight clears recent
thermal history and preserves current altitude to avoid a pressure jump. Pause and single-step
stop/advance both the world and firmware together. Loading a recording exits live mode.

**Save flight recording** downloads the recent sensor stream as a `.log` that can be replayed
through the normal recording player or sent through the device's existing UDP injection path.
Storage is bounded to 600 trail points (five minutes) and 60,000 sensor lines (roughly twenty
minutes). The flight model and UI are simulator-only; physical firmware memory use is unchanged.

Regression checks: `make -C sim test-free-flight test-thermal-core test-thermal-track test-display-settings` inside the
usual container.

`make -C sim test-udp-injection` checks real UDP packets, the developer-mode and injection-setting gates, persistence, and repeated disable/enable cycles without a reboot.
`node sim/tests/developer_card.cjs` checks the embedded web developer card's injection toggle and rejected saves.

Thermal Core shows a lift-weighted center after a complete turn with sufficient lift and spatial
coverage. Its thin geographic north/south and east/west lines rotate with the map and are clipped
inside its border. The estimate uses the latest lap's altitude-projected breadcrumbs, with angular
weighting and gentle smoothing; it estimates the sampled lift, not an unseen thermal core. Reveal
the live thermal to compare the estimate with the simulator's independent ground truth. Partial
turns, weak lift and nearly straight paths cannot establish a new center. Once established, the
last good center remains visible within 100 m, even when the recent trail cannot fit a new lap.
It hides after five continuous seconds beyond 100 m and restores on return within 100 m. The
retained center still projects to the current altitude and map bearing. Stale GPS temporarily
hides it and interrupts the departure timer; it does not erase the center.

Climb quality is signed, time-weighted net climb over the latest one or two completed,
non-overlapping turns. A candidate within 40 m adjusts the center using the existing 35% smoothing.
A candidate more than 40 m away switches the target directly only if its average climb is better
and its position stays within 15 m for three seconds. Equal or weaker candidates also smooth.
Incomplete fits retain the old center and its climb history. Fresh fits exclude connecting flight
after three seconds of straight flight or a confirmed 30-degree direction reversal. Only two lap
summaries are stored; the breadcrumb buffer stays at 40 samples.

For a coherent strong-side/weak-side lift gradient, the target's displacement from the geometric
orbit center is amplified by up to 4x, capped at roughly one directional orbit radius and the
sampled strong-side extent. Gain ramps above a 0.3 m/s contrast deadband and reaches its maximum
at a coherent 1 m/s contrast. Isolated spikes and scattered noise get no extra gain. Multiple
weaker observations surrounding the strongest region reduce gain to 1, retaining an interior
estimate when the peak is bracketed. All geometry uses the same altitude projection; the retained
target's slant includes the amplified displacement. Position smoothing and distance retention
remain unchanged.

A 30 m target circle is drawn over the breadcrumbs at the calculated lift center. It has two
black circles at its radius and one pixel outside, with white circles one pixel inside and two
pixels outside. At its center, two black triangles form a wind arrow with a fluted tail. A
six-pixel-radius white disc clears the breadcrumbs and crosshairs, leaving a white buffer around
the five-pixel-radius black outline. The arrow's tip and both tail tips touch the outline. It points
where the wind is blowing and rotates with the map; it is hidden without a valid wind estimate.
Auxiliary widening/tightening rings are removed and the top-of-map turn reticle is disabled.

ThermalTrack shares Thermal Core's upper layout, including the narrow vario, large climb field,
altitude with its selectable source, and wind circle with compass letters and speed. The selected
thermal's distance and strength share the strip from (15,57) to (95,73), with fixed positions and
font sizes. Strength and its unit align to the right edge; distance centers between fixed steering
arrow slots, with 6x12 numbers and 5x8 m/km/mi labels. Distances use feet/metres below 1,000,
tenths of miles/kilometres below 10, then whole miles/kilometres capped at 99. Steering arrows use
the same 20/40/60-degree thresholds as waypoint
navigation and are hidden without a selected thermal.

The pilot triangle's tip sits at the map center. A single-pixel line runs from its tip to the
selected thermal's outer selection ring, stopping before the marker's hollow interior. The
line is clipped to the map and connects directly to the pilot's tip.

Three formats, one playback path. Everything is normalised at load time into the bus-log line
format the device itself records (`dispatch/message_injector.h`), so a synthetic flight and a
captured one are replayed by exactly the same code.

| Format | What it carries | Where it comes from |
|---|---|---|
| `*.log` | Real captured GPS, IMU, pressure and ambient data | `BusLogger` on a device (Leaf Labs → start bus log) |
| `*.igc` | GPS fixes and pressure altitude at 1 Hz | Any flight logged by Leaf or another vario |
| `*.json` | Hand-authored flights: legs of heading, airspeed, climb and turn rate, in a wind | Written by you; `sim/recordings/` holds a thermal climb |

The two `.igc` examples are real winch flights at the same field, a few minutes apart.

`.igc` and `.json` recordings have no IMU data behind them, so the emulator synthesises level
flight at 1 g, carrying the vertical acceleration each change of climb rate implies. That is
enough for the firmware to finish its startup checks and for the vario's accelerometer fusion to
behave; it is not a substitute for a real recording if what you are testing *is* the IMU. Use a
device bus log for that.

Any loaded scenario can be written back out as a device-format bus log:

```sh
leafsim --scenario flight.igc --export-log flight.log
```

…which can then be replayed **against real hardware** with `sim/play_buslog.py`, the script that
already existed for injecting data into a device over WiFi. With `--setting DEV_MODE=1 --setting DEV_UDP_INJECT=1`, the emulator also listens on UDP 7431
itself, so that same script can drive the emulator with no changes.

## Headless runs and CI

For bounded runtime telemetry, add `--runtime-capture path/to/capture.json` to a finite run.
It records host thread CPU work separately from virtual timing and exports after freezing on
normal exit. Device heap/stack placeholders are omitted. See
[runtime assessment](../docs/dev-references/runtime-assessment.md) for report generation and
measurement limits. These reports are required for published firmware releases, not every PR.

The emulator runs without a browser, driven by a timed script, and exits with a non-zero status if
an expectation fails — which makes screen-level regression tests possible.

```sh
leafsim --port 0 --speed 0 \
        --scenario sim/recordings/thermal-climb.json --play \
        --script sim/scripts/first-boot.txt
```

A script is `<seconds> <command> [argument]` lines against device time:

```
20   screenshot    sim/build/01-warning.png
22   press         DOWN
24   press         CENTER
30   expect-page   User
31   expect-serial SD card: mounted
32   quit
```

Page names are the firmware's own `MainPage` values: `Debug`, `Debug2`, `Basic`, `User`,
`ThermalCore`, `ThermalTrack`, `Navigate`, `Menu`, `Charging`, `Blank`.

Commands: `press` / `down` / `up`, `hold BUTTON [ms]`, `screenshot`, `inject` (one bus-log
line), `scenario play|pause|seek`, `speed`, `status`, `expect-page`, `expect-serial`, `quit`.

How long a `hold` holds decides which of a button's hold actions fires: the firmware reports a
hold at 800 ms and then increments every 500 ms, so hold-to-power-off (increment 2) needs
`hold CENTER 1800` or more. The default is 1000 ms.

`--speed 0` runs the clock as fast as the host manages, so a 7-minute flight takes seconds. Because
time is virtual, a script produces the same run every time regardless of how loaded the machine is.

## When a screen shows nothing

The emulator reproduces the device's gating faithfully, so a blank field usually means the
firmware would blank it on hardware too. The three that come up most:

**Anything that needs a GPS fix** — glide ratio, ground speed, track, distance flown, IGC logging
and the flight timer's auto-start all sit behind `gps.hasUsableFix()`, which needs the receiver's
fix-quality field, not just a position. The Leaf's receiver is multi-constellation, so the firmware
reads that field from `GNGGA` (and fix mode from `GNGSA`); a recording carrying only `GP*`
sentences parses as a position but never sets the fix, and every one of those fields stays blank.
The scenario generator emits `GNGGA` / `GNRMC` / `GNGSA` / `GPGSV` for this reason. If you feed a
hand-made recording and the values are missing, check the talker IDs first.

**Thermal pages**: enable Thermal Core and Thermal Track in the Display menu. The setting
persists in `sim/state`. To preset the promoted display settings (bypassing legacy Labs migration):

```sh
leafsim --setting THERM_DISPLAY=1 --setting SHOW_THERM_TRK=1
```

`--setting` writes into the emulated device's saved settings before boot, using the NVS keys from
`ui/settings/settings.cpp`. It also marks the store as initialised, because a device that has never
saved settings writes its defaults over everything on first boot.

**Glide ratio while climbing** — glide is only defined going down. In a climb the firmware shows
`--.-`, which is correct; fly a sink leg and a number appears.

## HTTP API

Everything the panel does is available to scripts. All state-changing calls are queued and applied
on the device thread between passes through the firmware's `loop()`.

```
GET  /api/state              device status as JSON
GET  /api/frame              current screen, 1bpp base64, with a sequence number
GET  /api/events             server-sent events: frame, status, serial, tone
GET  /api/screenshot.png     PNG of the screen (?scale=N)
GET  /api/scenarios          recordings available to load
GET  /api/track              the loaded recording as [time, lat, lon, altitude] fixes
POST /api/button             {"button":"CENTER","action":"click"|"down"|"up"}
POST /api/clock              {"speed":10} {"paused":true} {"stepMs":500}
POST /api/scenario           {"load":"flight.igc"} {"play":true} {"seek":42.0}
POST /api/inject             {"line":"P1234,92310"}
POST /api/board              {"batteryPercent":42,"charging":true,"cardPresent":false}
POST /api/restart            reboot the emulated device (a real process restart)
```

`seek` moves the recording's cursor and nothing else. The firmware keeps the state it had already
reached -- barometer filters, GPS fix, navigation, the flight timer, the open log -- so seeking
back replays earlier data into later state, and seeking forward skips everything in between. To
land the device in the state a given moment of a flight would really have produced, restart and
replay to that time at `--speed 0`.

## What is real and what is not

The point of the emulator is that almost everything is the shipping firmware. What is replaced,
and why:

| Layer | Emulated as | Notes |
|---|---|---|
| Arduino core, ESP-IDF, FreeRTOS | `sim/hal` | Time is virtual; pins, ADC and the LEDC speaker channel are a virtual board |
| Buttons | Virtual GPIO pins | `hardware/buttons.cpp` is the real one, debounce and hold detection included |
| Display | Real u8g2, buffer read back | The pixels are the device's own |
| SD card | A host folder (`sim/sdcard`) | Real `File` semantics; IGC files, logbooks and settings land on disk |
| Settings (NVS) | Text files in `sim/state` | Survive restarts, and can be hand-edited between runs |
| GPS (LC86G) | Real driver, virtual UART | Recorded NMEA is read byte-by-byte, as from the receiver |
| Barometer (MS5611) | Not modelled | Pressure arrives as bus messages; `instruments/baro.cpp` and everything downstream is real |
| IMU (ICM-20948) | Not modelled | The part runs DMP firmware of its own; motion arrives as bus messages |
| Temperature (AHT20) | Not modelled | Ambient arrives as bus messages |
| BLE, FANET/LoRa, WiFi, webserver, OTA | Stubbed | They report themselves unavailable rather than pretending to work |

The barometer and IMU are injected at message level rather than at their register interfaces. That
is a deliberate trade: the drivers' I2C conversation is not what the firmware's behaviour depends
on, and the ICM-20948's fused output comes from a DMP core that would have to be emulated as a
second processor.

A fatal error inside the firmware halts the device in its "press a key to reboot" handler, exactly
as on hardware — the panel shows the error screen and the console line, and a button press reboots
it. Headless runs stop instead, print the error, and exit with status 3.

## Layout

```
sim/
  Makefile          the build: real firmware sources + HAL + emulator
  run.ps1 run.sh    container runners
  hal/              host stand-ins for the Arduino/ESP32 platform
  device/           replacements for firmware units that touch hardware directly
  emulator/         the runtime, scenario player, script runner, HTTP server
  web/index.html    the control panel
  recordings/       scenarios to play
  scripts/          timed scripts for headless runs
  sdcard/           the emulated SD card (created on first run)
  state/            emulated non-volatile settings (created on first run)
```

`play_buslog.py` is unchanged and still points at real hardware — or at the emulator.
