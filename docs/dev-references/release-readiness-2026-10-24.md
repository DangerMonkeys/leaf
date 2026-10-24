---
title: Upcoming Release Review, October 24, 2026
description: Changes since v0.2.4, outstanding implementation work, and remaining release validation.
---

# Upcoming release review, 2026-10-24

Comparison: `v0.2.4` (`b7d41c6ebc6f840752c5df98d5a5322c7a716892`) through refreshed
`origin/main` (`3b82015ab984886dc1ba8802162570e419d3a953`). All 24 commits reviewed.
The target is a review snapshot, not a selected/tagged/qualified release candidate. No version
number has been chosen here. Dates use America/Los_Angeles.

This updates the earlier release discussion and the October 22 backlog review. It uses current
source, tests, merged PR validation records and backlog notes. No firmware builds, hardware
tests or runtime captures were repeated during this documentation review. Branch-only work is
excluded. A public-facing draft is in [release-notes/upcoming.md](../../release-notes/upcoming.md).

## Additions since the previous discussion

| Merge | Implemented change | Remaining qualification or limitation |
| --- | --- | --- |
| [#428](https://github.com/DangerMonkeys/leaf/pull/428) | Two arrival-height fields; full remaining route distance for goal; missing elevations preserved; timer events preserve all five page cursors while shutdown clears them | Route/units/GPS-loss checks on device. Older files that already substituted zero for unknown elevation cannot be distinguished from real sea-level values. |
| [#429](https://github.com/DangerMonkeys/leaf/pull/429) | Core altitude-history graph and trailing-minute net climb; five-second samples, 25 m/pixel; collection follows center visibility even on another page | Simulator rendering, metric/imperial and lifecycle checks are recorded. Validate usefulness/readability in flight; disappearance of the center clears history. The widget occupies the space above the map. |
| [#430](https://github.com/DangerMonkeys/leaf/pull/430) | Phase 1 logbook checked saves, revision/completion metadata, recovery, bounded final-save retries, complete deletion; USB diagnostic stall fix | Meaningful device and FAT-backed checks passed. Physical power-cut resilience, full GPS/IGC flights, active-flight shutdown and target resource limits remain unqualified. |
| [#431](https://github.com/DangerMonkeys/leaf/pull/431) | Sustained g-load on both IMU paths; live filtered User field; flight-aware extrema or null; V2 interval diagnostics and final group before checksum | Flight thresholds remain experimental. Hovering/low-groundspeed flight, landing/relaunch and diagnostic SD/timing overhead require device assessment. |
| [#432](https://github.com/DangerMonkeys/leaf/pull/432) | Fading motion score combines fresh GPS speed and positive one-second climb; frozen 30-second launch history and start/end causes in IGC | Simulator calibration/regressions and five hardware builds passed; both OTA slots were flashed/hash-verified per PR record. That is not operational launch qualification. Auto-stop thresholds are unchanged. |

## Complete change summary since v0.2.4

- **Thermal displays:** refined altitude-projected lift-center estimates, wind/steering guidance,
  breadcrumbs, retention and layouts; Core/Track enabled through Display instead of Labs;
  optional Core switching/return; new climb history; faster Track drawing. Existing page
  visibility is migrated rather than forcibly enabling thermal pages for all upgraded users.
- **Navigation:** four configurable slots and nine choices. Goal time/distance/required glide
  follow the remaining route. Arrival surplus is GPS altitude minus destination elevation minus
  distance divided by the pilot's averaged glide ratio; unavailable inputs or negative results
  show `--`. Metric/imperial rendering and missing-height handling are covered in simulator tests.
- **Flight detection and g-load:** recent speed/climb score replaces the fixed start countdown;
  climb alone can start the timer without GPS, but cannot fabricate a track. Both IMUs feed the
  sustained-load filter. Launch exclusion and delayed flight-aware extrema reduce handling
  contamination; insufficient accepted history is null. IGC diagnostics retain launch inputs,
  end cause, interval peaks, filtering/gaps/clipping and accepted/discarded load history.
- **Flight files:** completed IGC checksums with a native/Windows checker; Phase 1 recoverable
  JSON saves and Leaf Log result updates; retained prior backup, candidate ordering/readback,
  bounded retries and save/delete failure feedback. Legacy records remain readable.
- **Leaf Log and charging:** sliced preparation and session queue reuse; 100 pending-flight cap;
  responsive background uploads; animated panels, compact counts, two-click cancellation,
  clearer USB/power-on states and linked-account website shortcut. Leaf Log still requires its
  existing Labs opt-in. Transient upload failure retains firmware ownership until retry or action.
- **Hardware and commissioning:** v3.2.8 LSM6DSV support, gravity/bias processing, saturation
  recovery and persisted flat startup reference; coherent GPS-fix satellite reporting and
  restoration of configured audio after self-test. Legacy ICM estimator migration is deferred.
- **Stability and interaction:** clear thermal state at flight start/stop/shutdown; skip altitude
  reads before barometer readiness; keep editing cursors independent of timer events; clear
  all page cursors on shutdown; bounded USB diagnostics prevent a monitored startup stall;
  GPS UART yields between characters at slot boundaries; HELLO splash restored.
- **Tools and support:** real-firmware interactive free-flight simulation and record/replay;
  LCD inspection controls; bounded runtime capture/report tooling and provisional baseline;
  CI caches and release-only standard build selection; explicit default-off UDP replay gate;
  updated developer/backlog/release guidance and the v3.2.8 hardware schematic. The separate
  browser training tool already existed.

## Work to finish before publication

Follow [the release checklist](releasing.md). The following are assessment needs, not assertions
that all other backlog features must be implemented before releasing.

| Area | Evidence already recorded | Remaining checks |
| --- | --- | --- |
| Exact candidate and runtime | Stage 1 capture/report tools and older device reports exist. Latest #432 PR checks passed all five release variants, formatting and tracker CI. | Choose version/tag and exact candidate; build/retain binaries, ELFs and hashes; review candidate simulator and device reports or record a maintainer-approved exception. Measure occupied/late slots, GPS/barometer timing, button recovery, heap/largest block/stacks and the new diagnostic write load. Assess flight, web and transitions separately. |
| G-load | Tracker/serialization CI, both-IMU integration, short/long flights, stale samples, interval extrema, landing/relaunch replay and IGC checksum tests recorded. | Fly from pre-launch through landing/packing with event times. Include hovering/low ground speed, top landing/relaunch and both sensor families. Review the three-airborne/twenty-ground checks, 15-second launch exclusion, delayed 30-second buckets and discarded real maneuvers. Decide whether to retain/gate temporary diagnostics after tuning. |
| Auto Start | Calibration cases trigger in 3–5 seconds; fused climb-only simulation in 4.8 seconds. Invalid/stale inputs, toggles, spikes, gaps, delayed GPS and start/end causes covered. | Real slow, vertical and short launches, noisy ground handling, walking/transport, GPS loss/recovery and interaction with summary/stop/relaunch. Check acceptable latency and false-start tradeoffs. Verify unchanged auto-stop behavior in the integrated build. |
| Logbook/SD | Fault injection/process-exit tests; real full Linux FAT volume; device no-fix saves, absent-card failure then new flight, staged recovery, deletion/cold boot. | Device GPS/IGC association, active-flight ordinary/low-battery shutdown, transient retry recovery, slow/nearly-full/multiple cards, peak resources and controlled power interruption. A 2-second retry budget cannot preempt a blocking filesystem call; same-card backups are not guaranteed power-loss durability. |
| Thermal and navigation | Core/Track, settings/persistence, arrival math/rendering, history and lifecycle simulator tests; earlier device bring-up/timing evidence. | Repeated shutdown/charge/wake/flight restart with populated thermal state; real weak/irregular lift, wind, reversals and GPS recovery; Core auto-return/manual override; widget units/visibility; route advancement, standalone goal, missing/zero/negative elevations and invalid glide on device. |
| Leaf Log/USB | Queue, retry, cancellation and ownership simulator suites, 46 charging captures, one successful monitored charge-to-on cycle after the USB fix. | Actual TLS/NTP/server failures, revoked token/relink, long 100-flight sessions, cancellation delays, slow/full SD, retry power cost, host mount/eject/suspend/cached writes, removal/reinsertion and repeated monitored startup. |
| v3.2.8 and older variants | v3.2.8 bench/commissioning/calibration and flash records; all five PR build variants pass. | Moving startup, sustained motion/rotation, temperature, multiple boards, current/fault and power cycles. Check representative legacy ICM hardware too; successful compilation is not a sensor/flight qualification. Dedicated commissioning Auto-Off timeout check remains. |
| Release/manual | Website release checklist and a prepared manual branch are documented; branch `codex/website-manual-release-refresh` exists at `7aba63bd`. | Reconcile that held manual/screenshots with #428–#432, then publish alongside firmware availability. Check final assets/version metadata and assessed binary hashes. The existing manual branch is not evidence that newer features are documented. |

The registered Navigate reference remains provisional and `review_required`. It covers an older
candidate and limited workload, with unresolved sensor lateness/button recovery and unverified
radio traffic. Preserve its historical evidence; do not use it as a current release pass or silently
rewrite its known issues. See [baseline instructions](../../utils/runtime_report/baselines/leaf-3_2_8-navigate-v1/README.md).

## Implementation backlog still open

| Backlog note | Current remaining scope |
| --- | --- |
| [SD writes](backlog/sd-card-write-robustness.md) | Phase 1 is merged and partially device-tested. Phase 2 still needs every-writer ownership/drain coordination, checked continuous IGC/bus logging, sync policy, recoverable profile/route/waypoint saves and broader power-loss testing. |
| [Accelerometer metrics](backlog/accelerometer-flight-metrics.md) | Filtering/diagnostics are implemented; flight validation and detector tradeoffs remain. |
| [Thermal Core](backlog/thermal-core-display.md) | Next-waypoint bearing cues on Core/Track and representative field validation remain. The space above the map is occupied by the implemented climb-history widget. |
| [Miscellaneous bugs](backlog/misc-firmware-bugs.md) | Thermal readiness/lifecycle fix exists; repeated physical lifecycle acceptance remains. Cursor cleanup is now correctly separate from timer events. |
| [IGC / XContest](backlog/signed-igc-xcontest-validation.md) | Authenticated signing, identifier registration, stable recorder identity, key/trust policy, validator deployment and contest acceptance remain unestablished. An unkeyed MD5 checksum does not complete them. |
| [Runtime assessment](backlog/runtime-resource-assessment.md) | Manual exact-candidate qualification now; automated burdened workloads, test station, thresholds and publication enforcement later. |
| [USB ownership](backlog/usb-mass-storage-power-on-ownership.md) | Broad physical host/FAT/transition matrix and all-storage lifecycle coordination remain. The USB diagnostic freeze has a focused fix and initial device confirmation. |
| [Leaf Log](backlog/leaf-log-auto-upload.md) | Physical/network qualification and retry power behavior; sleep during backoff and automatic fail-open USB handoff are not implemented. Orphan IGC migration and permanent token identity remain outside current scope. |
| [ST IMU](backlog/st-imu-magnetometer-migration.md) | Broader field/temperature/board/fault qualification. Magnetometer, gestures and legacy estimator migration remain deferred. |
| [IMU gravity](backlog/vario-imu-gravity-performance.md) | Optional ICM Kalman/bias migration and wider unit/temperature characterization; do not re-list the older implemented ICM clamp fix as missing. |
| [Commissioning](backlog/factory-commissioning-followup.md) | Derive NVS erase boundaries from partitions, dedicated Auto-Off timeout testing and wider calibration qualification. |
| [Heap](backlog/heap-availability-improvements.md) | Measured stack reservations/allocation/service lifecycle tuning and long-run qualification. Existing streamed responses and upload queue reuse are already implemented. |
| [Diagnostics](backlog/diagnostic-files-cleanup.md) | Download/clear UI where useful, CSV/runtime retention/rotation, decisions about temporary diagnostics and checked writes. |
| [LCD preview](backlog/lcd-preview-renderer.md) | Additional reusable page/WiFi/QR fixtures and physical LCD readability checks. Real-code simulator rendering already exists. |
| [IGC task rendering](backlog/igc-task-start-rendering.md) | Casual-viewer declaration/first-point compatibility; checksum validation does not establish correct map rendering. |
| [Path distance](backlog/logbook-path-distance.md) | Flight distance still integrates GPS speed once per second; point-to-point accumulation/jitter handling remains separate from route-distance improvements. |
| [Bootloader](backlog/one-button-bootloader-entry.md) | Application BOOT long-press behavior and physical strap/reset verification remain proposals. |
| [Waypoints](backlog/waypoint-handling-improvements.md) | CUP short/rich names, selector position context, grouping, capacity/storage options and GPX name-only policy. Missing-height preservation is now implemented. |
| [Web firmware checks](backlog/web-app-firmware-check-followup.md) | Passive checks, session result caching and starting OTA from the web remain proposals; current user-triggered checks already exist. |

## Documentation reconciliation, 2026-10-24

- The October 22 reconciliation now points to this review and updates its affected backlog rows
  for implemented g-load filtering, Phase 1 SD hardening and Core history/cursor behavior.
- The SD backlog distinguishes merged Phase 1 work and completed device checks from remaining
  power-loss/resource qualification. Its original weakness table is explicitly historical.
- Core documentation reflects the history widget and timer-independent editing cursors.
  Leaf Log and USB notes describe checked result updates and the monitored startup fix.
- G-load, diagnostic and runtime notes distinguish implementation/flash evidence from flight
  qualification and account for the new normal-IGC diagnostic workload.
- PR #413 remains open although #431 states it integrates that implementation and follow-ups.
  Reconcile/close the older PR after checking for any remaining delta; it should not automatically
  be counted as an unimplemented g-load feature. No GitHub PR state was changed by this cleanup.

## Review coverage and limits

The public draft's HTML comment maps all 24 commits to bullets or intentional internal-only
omissions. Supporting sources were the cumulative code/test diff, individual mixed commits,
all 19 backlog notes, the release/runtime guides and merged #429/#430/#432 PR records.
This review prepares documentation only; it does not certify a release or publish changes.
