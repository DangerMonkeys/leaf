# Barometer not ready after charging-mode wake

The SD `/fatal_error_1.txt` was copied and verified byte-for-byte before further investigation.
[Raw evidence](startup-fatal-error-1.txt), [decoded backtrace](startup-fatal-error-1-decoded.txt),
and [binary/ELF provenance](startup-fatal-error-1-metadata.json) are retained.
The installed firmware was not changed, rebuilt or reflashed during this diagnosis.

## Confirmed failing call

`TaskManager::doNecessaryTasks` -> `ThermalCore::update` -> `Barometer::altAdjusted` -> fatal
`Barometer::altAdjusted while WaitingForFirstReading (1)`.

The exact flashed ELF resolves the altitude read to `src/vario/navigation/thermal_core.cpp:261`.
ThermalCore runs when its feature is enabled, even if the currently displayed page is Navigate.
Its update returns early for empty thermal history but does not check barometer readiness.

## Mechanism and reproduction

Thermal sample history survives a soft shutdown into USB charging because the MCU remains
powered. `Power::sleepPeripherals` calls `baro.sleep()`, which resets its startup state and
20-reading discard counter. On wake, `baro.wake()` enters WaitingForFirstReading. ThermalCore
can run using retained history before those readings finish. Its unguarded adjusted-altitude
read then fails the barometer's state assertion.

An [isolated simulator reproduction](startup-fatal-repro.cpp) builds the unchanged firmware/HAL
objects, fills real thermal history from scenario playback, pauses input, calls `baro.sleep()`
and `baro.wake()`, verifies history remains and barometer waits, and calls `thermalCore.update()`.
It reproduces the **exact fatal message**, with the simulator's expected fatal exit code **3**.
[Simulator output](startup-fatal-repro.txt) is preserved. This establishes a sufficient failing
sequence. The SD stack does not encode the entire user's preceding power-state history.

The unguarded read predates this branch's telemetry and UART changes. Git attributes its
introduction to `97ff8083` (Improve thermal guidance and layouts with live simulator training,
#415); identifying its origin does not establish which timing change made this occurrence visible.

## Recommended correction

Require barometer readiness before ThermalCore reads live adjusted altitude. While unavailable,
clear/invalidate the displayed guidance and return; resume when startup completes. Consider
explicit thermal-history/guidance reset at the power-state boundary as a separate behavior choice.
Keep the barometer assertion: returning a made-up altitude would hide invalid sensor state.
Add regression coverage for retained samples during sleep/wake and successful recovery when
pressure becomes ready. No fix was applied during diagnosis, to preserve the pending timing run
on the same installed binary.

A full power cycle, with USB disconnected and the unit completely off, clears retained RAM history;
this is a workaround for this sequence, not a substitute for a readiness guard.
