---
title: Miscellaneous Firmware Bugs
description: Confirmed firmware defects and focused follow-up fixes.
---

# Miscellaneous firmware bugs

## Thermal state survives shutdown and flight stop; altitude read before barometer ready

Status: fix merged to `main` in `dd70c883` (#424); hardware evidence corrected in `9919484e`.
Simulator lifecycle,
Thermal Core/Track and display regressions and the `leaf_3_2_6_release` and
`leaf_3_2_8_release` firmware builds pass.
Reported and reproduced on Leaf 3.2.8 on 2026-10-21. Repeated hardware lifecycle checks
remain required before closing this item. The matching `leaf_3_2_8_release` candidate was
flashed to both OTA slots on the connected Leaf 3.2.8, with both write hashes verified.
Serial output confirms the main loop runs and handles the center button. An earlier default
3.2.6 flash used the wrong motion-sensor driver for this device and stalled in IMU initialization;
verified flash writes alone did not establish a successful hardware boot.

### Historical evidence and cause (before #424)

- Device fatal: `Barometer::altAdjusted while WaitingForFirstReading (1)`.
- The exact flashed ELF resolves the caller to `ThermalCore::update()` in
  `src/vario/navigation/thermal_core.cpp`, at the live `baro.altAdjusted()` read.
- ThermalCore returned early for empty history but did not check barometer readiness before
  using retained samples. It runs when the feature is enabled, regardless of the visible page.
- A soft shutdown into USB charging leaves the MCU and thermal history in RAM. The barometer
  resets its startup readings on sleep and waits for fresh readings on wake. ThermalCore can
  access altitude during that waiting period and trigger the state assertion.
- An isolated simulator reproduction fills thermal history, sleeps/wakes the barometer, then
  calls ThermalCore update. It produces the same fatal error. This path predates the runtime
  telemetry and character-by-character UART yielding changes.
- `flightTimer_start()` reset ThermalTracker and ThermalCore; flight stop and
  shutdown did not consistently discard all thermal state.

The verified SD log, decoded stack, build identity and simulator reproduction are in
`codex_artifacts/runtime-telemetry/2026-10-21/revision-4/startup-fatal-findings.md`.

### Implemented correction (#424)

1. Guard ThermalCore's live barometer reads with an explicit readiness check. Invalidate
   guidance while the barometer is unavailable and resume only when readings are ready.
   Audit other thermal-page consumers for equivalent assumptions; retain the barometer's
   state assertion rather than substituting an invented altitude.
2. Make **all thermal pages discard their saved runtime state on shutdown and flight-log
   stop**, including Thermal Core and Thermal Track. Clear recent sensor/thermal histories,
   detected/saved session thermals, retained centers and targets, fitted turns, guidance,
   breadcrumbs, page selection/cursor caches, and automatic page-switch/return state as
   applicable. Audit each owner instead of assuming the two existing reset calls cover all
   page-local or display-owned state.
3. Cover manual and automatic flight stop and every orderly shutdown path, including USB
   charging and low-battery shutdown, even when no flight is active. Keep resets idempotent,
   and order them so final flight statistics and summary/log finalization remain valid.
   Preserve user preferences and completed flight files; the reset concerns runtime thermal
   state. Keep the existing flight-start reset as a defensive boundary.
4. Ensure a stopped flight cannot immediately repopulate cleared thermal state from stale
   GPS/sensor data. Define when fresh collection may restart, including intentional thermal
   operation without an active flight timer, and keep the documented lifecycle consistent.

### Acceptance checks (simulator covered; repeated device cycles pending)

- Populate Thermal Core and Thermal Track state, shut down into charging, then wake before
  the barometer finishes startup: no fatal error and no previous-session guidance is shown.
- Stop a populated flight manually and automatically: both pages and automatic page-switch
  state are cleared before another flight. Repeated stop/shutdown calls remain safe.
- Repeat with no active flight, both thermal pages enabled/disabled, another page visible,
  stale GPS data, and delayed/missing pressure updates. No invalid sensor read is attempted.
- Verify fresh pressure/GPS recovery produces new valid guidance; final flight summaries,
  archived logs, configured page preferences and ordinary logging remain correct.
- Run simulator lifecycle regressions and verify repeated shutdown/charging/wake and
  flight-stop/restart cycles on hardware before closing this item.

### Implementation

- Flight start, manual/automatic stop and every orderly shutdown share a reset that clears
  ThermalTracker history, saved thermals and navigation selection; ThermalCore fits, targets,
  guidance and breadcrumbs; Basic/User/Core/Track page cursors; and display auto-switch state.
  Shutdown also resets sessions with no active flight. Preferences and completed logs remain.
- Collection can restart without an active timer, but only after a newly received usable GPS
  position and valid climb readings. GPS snapshots retain the position receipt timestamp;
  refreshing a snapshot cannot make a pre-reset position look new.
- The on-device `baro` call audit found missing readiness guards in ThermalCore updates,
  IGC record writes, flight-stop initial-altitude reset, shared altitude rendering and Core/Track
  altitude rendering. These now skip unavailable sensor reads, clear Core guidance or draw
  unavailable altitude as appropriate. Existing climb validity helpers include readiness;
  GPS altitude synchronization checks readiness internally. Barometer assertions remain intact.
- `make -C sim test-thermal-lifecycle` covers populated manual/automatic stops, final logbook
  data and retained track files, repeated stops, stale GPS, delayed pressure, all four page
  cursors, auto-switch state and charging shutdown/wake with active/non-logging sessions,
  enabled/hidden thermal pages and normal/low-battery shutdown.
