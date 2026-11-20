---
title: Vario Timing Robustness Backlog
description: Protect Leaf’s climb estimate and vario audio from storage and network stalls, and verify recovery when sensor data goes stale.
---

# Vario Timing Robustness Backlog

**Status:** Open. Field evidence from November 14, 2026, with a
[November 20 live-radio follow-up](../shutdown-radio-test-2026-11-20.md).
That follow-up verifies shutdown/lifecycle behavior, not flight-time vario robustness.
Keep stack reservations unchanged while isolating timing effects; further memory
reductions are deferred in the [restart plan](memory-optimization-followup.md).

Keep the vario responsive to real climb and sink while a flight is recording,
the SD card is slow, or the radios are busy. The pressure and IMU sampling,
climb estimate, and speaker timing currently depend on the same main loop
that writes logs and services the Web App. A blocked loop can delay the next
sensor read and leave the speaker playing the last commanded pitch.

## Field evidence

A 59:51 IGC from a Leaf 3.2.6 running `0.2.4-865d` passed its checksum. At
about 21:30 and 21:46 after recording began, the ICM-20948 FIFO exceeded its
drain limit and reset. The second reset coincided with a 750 ms pressure gap
and a 780 ms motion gap. Both gaps began within 20 ms of the same five-second
System Events checkpoint, which appends to the SD card synchronously. The
checkpoint recorded its start time. Its write duration remains unmeasured.
An earlier, separately measured 470 ms IGC write was also followed by a FIFO
reset.

Nearby heap samples showed about 95.5 KiB free with a 31 KiB largest free
block. The stack session recorded 7.5 KiB unused on the main loop and about
2.1 KiB unused on the Bluetooth worker. Neither FANET worker ran.

Later vario swings had no corresponding events at the IGC diagnostic
thresholds. The IGC samples the one-second climb average, while audio and
the configurable display average have different response times. Keep the
longer swings open for investigation. Use time since recording start when
aligning diagnostics. The track suggests takeoff was about 11 minutes into
this recording. [IGC event definitions](../flights_and_logging.md#event-records)

## 1. Isolate in-flight SD writes

- Keep periodic System Events checkpoints in a fixed-size RAM buffer
  during recording. Save after the flight under firmware SD ownership.
  Reuse the existing bounded diagnostic history where it fits and make
  overwritten samples visible in the exported report.
- Hand ordinary IGC output to a lower-priority writer through a bounded
  queue. Give the writer ownership of the file and preserve record order
  and checksum behavior. The flight loop should return immediately when
  the queue is full. A full queue, short write, or unavailable card should
  surface a recording problem and preserve any recoverable data while the
  vario keeps running.
- Coordinate drain, close, and finalization with flight stop, the existing
  shutdown deadline, and USB mass-storage handoff. Measure buffer and
  worker stack costs as part of the change.
  [SD card write robustness](sd-card-write-robustness.md)
  · [Bounded shutdown](../bounded-shutdown.md)

## 2. Protect sensor and audio timing

- Give pressure acquisition, IMU polling, climb estimation, and tone
  scheduling a dedicated higher-priority task. Preserve the pressure
  conversion schedule and publish the latest timestamped estimate to
  the display, logger, and radios.
- Keep storage, networking, and other variable-duration work outside
  the task. Audit shared I²C access and synchronous message-bus callbacks
  so the task cannot inherit an unbounded wait from another subsystem.
- Enforce output freshness inside the protected task. Use the current
  500 ms sensor freshness limit as the initial behavior. Silence the
  vario tone when the estimate expires, then resume when valid data
  returns. Keep the current estimator and stack allocations for the
  first comparison so changes in timing can be evaluated independently.

## Verification

1. Run a comparison with normal IGC and Stacks enabled and System Events
   disabled. Record elapsed times from IGC start and whether symptoms
   affect the audio, display, or both.
2. Replay a known climb and sink profile under a deliberate 0.5 to 2 s
   storage stall. Compare sensor cadence, estimate age, climb-rate error,
   and actual speaker pitch and beep timing with the same replay without
   a stall. A storage stall should leave the beep response tracking the
   reference.
3. Interrupt pressure and motion inputs separately. Verify that an expired
   estimate goes silent within the freshness window and that valid samples
   restore the expected tone without a false climb on recovery. Measure
   recovery latency and record short gaps as well as resets.
4. Repeat on hardware with both supported IMU paths, a connected Bluetooth
   client, and active FANET traffic where available. Exercise menu entry,
   slow or unavailable SD storage, and flight stop and shutdown with writes
   pending. Check IGC order and checksum, recording-error handling, timing,
   heap, and stack headroom for the tested build.

If the longer swings recur, capture sensor age, main-loop delays,
estimated climb, and tone changes in a bounded RAM trace for export after
the flight. Compare those signals before changing estimator tuning.
[Runtime assessment](../runtime-assessment.md)
· [IMU gravity performance](vario-imu-gravity-performance.md)
