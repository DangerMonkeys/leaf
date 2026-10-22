# Stage 1 smoke evidence — 2026-10-21

These are manual smoke assessments of uncommitted work on `codex/runtime-telemetry`,
based on commit `7ed9b81337dcbccb37845a3e81c06fe2892ef596`.
They are **not approved operational baselines or release certification**.

- [Device report](device.html): Leaf 3.2.8, ESP32-S3-FN8, real sensor/GPS drivers,
  55.15 seconds, at least 55 observations in each of the 100 logical slots.
- [Simulator report](simulator.html): matching `leaf_3_2_8` configuration,
  thermal-climb recording, fresh state, 120 virtual seconds. Host CPU costs are uncalibrated. The simulator uses the repository placeholder firmware
  version; the explicit build variant, executable hash and source manifest identify this run.
- Each report has a matching JSON, raw capture JSON, and metadata JSON. Artifact hashes and
  source identity are included. The source manifests referenced by each metadata file identify
  its working files; neither report should be attributed to the base commit without its uncommitted modifications.

## Device observations

- Capture buffer: 17,688 bytes of internal RAM, with a 32 KB reserve checked before allocation.
  Successful SD export releases the buffer.
- Highest observed occupied time: 8.314 ms in slot 42 (IMU/wind). Occupied time includes
  blocking and preemption; this is not an isolated CPU computation measurement.
- Six deadline crossings, all in slot 41. In the retained worst event, assigned tasks ended
  about 0.59 ms after the intended start; GPS work ended 0.484 ms after the 10 ms deadline.
  The preceding slot finished useful work before its deadline. No timer ticks were coalesced.
- Slot 98 also had a 7.495 ms late start and a 7.540 ms barometer first-call delay. The trace
  does not establish the cause of that late start; investigate tighter conversion timing.
- Internal free heap: 96,020 bytes at both endpoints; boot-lifetime minimum 95,576 bytes;
  largest free internal block 31,732 bytes. No allocation failures. Loop stack headroom was
  8,452 bytes. Other task stacks were unavailable in this configuration.
- Observed maximum clock-pair interval 1 µs, slot bookkeeping 150 µs, memory sampling 519 µs.
  These include elapsed interference and do not prove a complete instrumentation overhead bound.

BLE, FANET, and thermal pages were disabled. Flight IGC output and network activity were not
verified. The operator confirmed START/STOP and the Save Timing confirmation sound. Normal
fully burdened operation, calibration, recovery, and soak workloads remain deferred.

## Verification

- Formatter and whitespace checks.
- PlatformIO `leaf_3_2_8_release` build; the device-captured image was flashed to both OTA slots
  and verified by esptool. Its exact hash remains in device metadata. The final simulator
  background-thread safeguard was checked in a later build; no Espressif port was available
  at the final scan, so that later image was not flashed or attributed to the device capture.
- Bounded accounting tests, including inherited lateness, GPS overrun, coalesced ticks,
  ring wrapping, partial slots, and 64-bit timing; also run under ASan/UBSan.
- FN8 allocation/export lifecycle tests: no-PSRAM fallback, reserve refusal, no per-slot
  allocation, refusal to export while active, frozen export and buffer release/restart.
- Simulator physics, thermal core/track, and display/settings regressions.
- Eleven report tests, including corrupt/missing data, compatible baseline checks, simulator
  labeling, HTML escaping, memory history, background-work coverage, and protection against
  overwriting raw evidence. Simulator receiver-thread callbacks cannot mutate loop-owned data.

Publication requires fresh reviewed reports for the actual clean tagged release artifact;
see [the release workflow](../../../docs/dev-references/releasing.md).

These historical captures use schema 1. Reports have been regenerated with the revised reader
and explicitly mark page timing and interaction filtering as unavailable. Raw files and their
hashes remain unchanged. See [schema 2 validation](revision-2/README.md) for the updated report.
