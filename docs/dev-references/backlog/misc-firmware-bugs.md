---
title: Miscellaneous Firmware Bugs
description: Confirmed firmware defects and focused follow-up fixes.
---

# Miscellaneous firmware bugs

## Thermal state survives shutdown and flight stop; altitude read before barometer ready

Status: open. Reported and reproduced on Leaf 3.2.8 on 2026-10-21. Fix and lifecycle
regression coverage are pending; recording this item does not change the firmware.

### Evidence and cause

- Device fatal: `Barometer::altAdjusted while WaitingForFirstReading (1)`.
- The exact flashed ELF resolves the caller to `ThermalCore::update()` in
  `src/vario/navigation/thermal_core.cpp`, at the live `baro.altAdjusted()` read.
- ThermalCore returns early for empty history but does not check barometer readiness before
  using retained samples. It runs when the feature is enabled, regardless of the visible page.
- A soft shutdown into USB charging leaves the MCU and thermal history in RAM. The barometer
  resets its startup readings on sleep and waits for fresh readings on wake. ThermalCore can
  access altitude during that waiting period and trigger the state assertion.
- An isolated simulator reproduction fills thermal history, sleeps/wakes the barometer, then
  calls ThermalCore update. It produces the same fatal error. This path predates the runtime
  telemetry and character-by-character UART yielding changes.
- `flightTimer_start()` currently resets ThermalTracker and ThermalCore; flight stop and
  shutdown do not consistently discard all thermal state.

The verified SD log, decoded stack, build identity and simulator reproduction are in
`codex_artifacts/runtime-telemetry/2026-10-21/revision-4/startup-fatal-findings.md`.

### Required correction

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

### Acceptance checks

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
