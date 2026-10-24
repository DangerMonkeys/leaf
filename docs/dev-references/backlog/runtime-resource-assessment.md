---
title: Runtime Resource Assessment Backlog
description: Release timing and memory assessment, implementation stages and follow-up work.
---

# Runtime resource assessment

Status reconciled **2026-10-22**: stage 1 is implemented and merged in `e432ee66` (#421).
A provisional Leaf 3.2.8 Navigate comparison reference is registered in
[`utils/runtime_report/baselines/index.json`](../../../utils/runtime_report/baselines/index.json).
It remains `review_required`, with unresolved timing/recovery and workload-coverage limits;
selection is not a release pass. Reviewed operational qualification and subsequent automation
remain pending. Reports are required for the exact firmware release candidate, not per PR.

The newer Core climb-history widget (#429), checked logbook finalization (#430), sustained
g-load/interval diagnostics (#431) and launch-history diagnostics (#432) are not qualified by
that older Navigate baseline. Include their actual activity, SD-write load, save stalls and
memory use in the candidate assessment, alongside the existing flight/web/transition workload.
The additional records are part of normal IGC logging, not only Developer-mode CSV capture.

Use [Runtime assessment](../runtime-assessment.md) for current capture/export instructions and
the [versioned baseline instructions](../../../utils/runtime_report/baselines/leaf-3_2_8-navigate-v1/README.md)
for compatible comparisons. Preserve existing baseline bundles and their known issues.

## Intent

Detect firmware changes that erode responsiveness or memory headroom before they reach
users. Leaf schedules work in 100 repeating 10 ms slots. Measurements must distinguish
expensive work from a late start caused by earlier work, include GPS and foreground
services, and expose accumulated timer ticks without changing scheduler behavior.

## Stage 1: bounded telemetry and baseline reports

Implemented scope is listed below. These are no longer outstanding implementation tasks;
broader qualification remains open under stages 2 and 3 and the baseline's attention items.

- Replace synchronous per-second CPU CSV writes with fixed-capacity RAM capture and
  explicit freeze/export. Keep production IGC writes in the measured workload.
- Capture intended and actual starts, assigned-task completion, GPS and service time,
  deadline misses, timer sequence gaps, and recording overhead. Preserve logical slot
  numbers separately from physical timer ticks.
- Capture bounded memory samples and recent detailed slot records. Disclose buffer size,
  overwritten trace records, unsupported simulator measurements, and incomplete runs.
- Generate self-contained HTML and JSON baseline reports with a labeled 10 by 10 slot
  grid, duration and lateness indicators, worst-event context, memory headroom, metadata,
  and optional comparison with a compatible baseline.
- Keep one firmware image, with no capture-buffer allocation or sampling while disabled and
  inexpensive inline hooks. Capture start requires Developer mode.
- Summarize each non-menu page's render cost and enclosing slot cost. Exclude button-affected
  slots from normal statistics; preserve them and delayed successor slots in separate recovery
  summaries, including sensor timing warnings. Operational recovery remains a hardware check.
- Add bounded capture-only GPS sentence type/validity/stage summaries, dispatch phase and
  retained deadline-crossing events. Distinguish observed UART reads from true wire arrival,
  and include exclusive mutex waits without double-counting callbacks. Preserve normal idle
  memory use by rebalancing short histories rather than adding permanent profiling buffers.
- Validate accounting with synthetic timing cases and simulator integration; build and
  attempt normal hardware verification. Do not claim a full operational assessment from
  a short smoke test.
- Document the release workflow in `../releasing.md`. Require reviewed simulator and
  device reports for the release candidate before publication, or an explicit documented
  exception. Stage 1 capture is operated manually.

## Stage 2: repeatable automated workloads (deferred)

- One versioned workload specification with simulator and hardware backends; configure,
  exercise, freeze, retrieve, and restore the device through a small control interface.
- Exercise flight with BLE notifications, FANET traffic, IGC logging, navigation, full
  thermal/wind histories, demanding screens, and real sensor drivers. Exercise web-app
  traffic and WiFi scanning in separate supported modes: the web app currently stops BLE
  and FANET. Include repeated transitions and separately assessed recovery cases.
- Verify activity through counters and output artifacts, not only enabled settings. UDP
  replay bypasses UART ingestion and changes execution context; retain real-driver runs.
- Use release-equivalent builds and calibrate simulator computation estimates against
  hardware. Keep sanitizer/debug passes separate from performance measurements.
- Add explicit stack-availability flags so an unregistered task and a measured zero-headroom
  stack cannot be confused in memory summaries.
- Add a controlled test station: dedicated Leaf/SD card, WiFi, BLE client, FANET peer,
  reset/power recovery, serial capture, and exclusive runner ownership. Consider a UART
  injector fixture for reproducible serial load. Add extended soak runs for leaks.

## Stage 3: release enforcement (deferred)

- Automate assessment attachment and validation for a fixed release commit and firmware
  artifact. Reports gate publication, not pull requests.
- Establish reviewed baselines and hardware-specific reserves before enforcing regression
  thresholds. Compare like-for-like workloads, toolchains, settings, and hardware.
- Preserve failures, incomplete coverage, waivers, and raw evidence. An unavailable device
  or lost essential telemetry is not a passing assessment.
- Add release automation only after instrumentation overhead, workload repeatability, and
  baseline noise are characterized. No automated workloads, PR gates, or publication
  automation are part of stage 1.

## Measurement principles

Use fixed storage and bounded work in critical paths: no formatting, allocation, SD writes,
or blocking locks. Freeze before exporting. Quantify observer overhead and storage use;
do not silently subtract them from reported resource pressure. Report elapsed occupancy
separately from CPU work, and observed maxima separately from proven worst-case limits.
Retain tighter sensor timing requirements in addition to the 10 ms slot budget.

The compact capture buffer uses internal RAM,
check a reserve before allocation, disclose its footprint and memory type, and release storage
after successful SD export.

Memory reporting should include internal heap free/minimum/largest block,
allocation failures where supported, and registered-task stack headroom. Simulator heap
and stack placeholders must never be presented as device measurements. Expensive heap
tracing and corruption diagnostics belong in separate diagnostic runs.
