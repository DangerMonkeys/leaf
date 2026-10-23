---
title: Backlog Reconciliation, October 22, 2026
description: Review of the October 21–22 main-branch work against all existing backlog notes.
---

# Backlog reconciliation, 2026-10-22

Reviewed `main` at `18fc69afdb958460c54b93d3fd40032d746c7521`. The working tree was clean
at review start. Dates here use America/Los_Angeles: most of the work landed October 21,
with the final two commits just after midnight October 22. The review includes all 13 commits
after `82a74de7`, and compares every one of the 19 existing backlog documents with current
source, simulator coverage and recorded validation. Older completed work was also reconciled
where a note still described it as unimplemented.

## Main-branch changes reviewed

| Commit | Change and backlog impact |
| --- | --- |
| `97ff8083` (#415) | Thermal guidance/layout and live simulator training; replaces the original Core prototype/host-preview proposals |
| `a6e069a6` (#417) | Configurable navigation fields, current remaining-route/goal metrics, Display controls and automatic Core switching |
| `d2c1d9a9` (#416) | v3.2.8 LSM6DSV support, Kalman bias/flat reference, commissioning and GPS/audio fixes; broader physical qualification stays open |
| `2b9cb5e1` (#418) | Sliced Leaf Log preparation and reusable session queue; 100-flight cap |
| `9946caa8` (#419) | CI dependency/compiled-file caches; does not complete runtime enforcement or hardware qualification |
| `c840faf9` (#420) | Retired dev environments/startup trace; runtime Developer/injection gates remain |
| `7ed9b813` | SD write investigation and phased plan; implementation remains pending |
| `e432ee66` (#421) | Bounded runtime capture/reports, GPS UART yielding and a registered provisional comparison baseline |
| `6c51405c` (#422) | Leaf Log website shortcut for linked devices |
| `824965f4` (#423) | Simulator LCD controls/device-panel polish |
| `dd70c883` (#424) | Thermal lifecycle/readiness fixes and Basic/User naming; repeated hardware lifecycle checks remain |
| `9919484e` | Corrected the device validation record to distinguish the matching v3.2.8 image from an earlier wrong-driver flash |
| `18fc69af` (#425) | Animated charging dialogs, live input, two-click cancel confirmation and USB/power-on states |

## Every existing backlog note

“Implemented” refers to source behavior and the cited records, not a fresh hardware qualification
or release approval. Test plans retained in notes are not a claim that every case has passed.

| Note | Reconciled status / remaining work |
| --- | --- |
| [Accelerometer flight metrics](backlog/accelerometer-flight-metrics.md) | Filtering/spike rejection remains open; log statistics still sample acceleration extrema. Updated the User Page name. |
| [Diagnostic files cleanup](backlog/diagnostic-files-cleanup.md) | Category toggles, consolidated files and runtime download exist; CSV management, rotation and retention remain open. |
| [Factory commissioning](backlog/factory-commissioning-followup.md) | Existing completion record matches code. Partition-derived NVS erase, dedicated Auto-Off timeout testing and broader calibration qualification remain open. |
| [Heap availability](backlog/heap-availability-improvements.md) | Runtime memory evidence and queue reuse exist. Profile/logbook responses already stream. Stack sizing, measured allocation/lifecycle tuning and long runs remain open. |
| [IGC task start rendering](backlog/igc-task-start-rendering.md) | Viewer compatibility remains open; clarified explicit role overrides/placeholders. A checksum validator does not test task rendering. |
| [LCD preview](backlog/lcd-preview-renderer.md) | Real-code host rendering and screenshot exports exist in `sim/`; additional fixtures and physical LCD checks remain. |
| [Leaf Log auto-upload](backlog/leaf-log-auto-upload.md) | Uploader, NVS credentials, result/UI handling and USB coordination exist. Updated session cap, retry policy, cancellation and actual timeout limits; physical/network/storage qualification remains. |
| [Logbook path distance](backlog/logbook-path-distance.md) | Still accumulates GPS speed once per second in `logging/log.cpp`; point-to-point distance/jitter handling remains open. Route-goal distance changes do not fix this statistic. |
| [Miscellaneous firmware bugs](backlog/misc-firmware-bugs.md) | Thermal fix merged; simulator/build/flash evidence preserved. Repeated device lifecycle checks remain open. |
| [One-button bootloader entry](backlog/one-button-bootloader-entry.md) | No application BOOT long-press handler found; proposed flow and hardware strap/reset verification remain open. |
| [Runtime resource assessment](backlog/runtime-resource-assessment.md) | Stage 1 and provisional Navigate reference exist; operational qualification, automated workloads and publication enforcement remain open. |
| [SD card write robustness](backlog/sd-card-write-robustness.md) | Both hardening phases remain open. Current finalization still ignores save failure, and replacement/recovery still lack the proposed checks. USB ownership does not complete this work. |
| [Signed IGC / XContest](backlog/signed-igc-xcontest-validation.md) | XLV/MD5 G records and local/native Windows validation exist (#414 and `82a74de7`). Authenticated signing, assigned registration, trust/key policy and contest deployment/acceptance remain unestablished. |
| [ST IMU migration](backlog/st-imu-magnetometer-migration.md) | Initial support merged; dated bring-up records retained as history. Moving-start/flight, temperature, multiple-board, current/fault and broader retention qualification remain. Magnetometer, gestures and ICM estimator migration are deferred. |
| [Thermal Core display](backlog/thermal-core-display.md) | Core, lift-center/target guidance, actual glyphs/layout, Display controls and lifecycle reset exist; representative field evaluation remains. |
| [USB ownership](backlog/usb-mass-storage-power-on-ownership.md) | Selected ownership/power-on path exists; obsolete alternative product proposals removed. Physical USB/FAT/cache and complete storage-user qualification remain. |
| [IMU gravity performance](backlog/vario-imu-gravity-performance.md) | ICM clamp/gate fix is historical and implemented; corrected historical wording. Optional ICM Kalman migration and broader unit/temperature characterization remain open. |
| [Waypoint handling](backlog/waypoint-handling-improvements.md) | Existing RAM/name/scroll completion record matches code. CUP naming, selector context, capacity/storage options and GPX name-only policy remain open; parser still resolves name-only route points. |
| [Web firmware checks](backlog/web-app-firmware-check-followup.md) | Existing button-driven check matches code; passive checks, result caching and starting OTA from the web app remain proposals. |

## Cross-document corrections

- Prepared the user-manual updates separately on `codex/website-manual-release-refresh`:
  Display-based thermal pages, automatic switching, configurable navigation fields,
  charging-only USB access, the 100-flight session cap, cancel confirmation and Leaf Log
  guidance, with current simulator and phone screenshots. This website/manual work is held
  for the next firmware release and is not included with the backlog-only documentation PR.
- Marked `src/vario/TODO.h` as historical so its early issue list does not present existing USB,
  OTA, Bluetooth, IMU, navigation and logbook features as a current implementation backlog.
- Kept baseline bundles and historical measurement artifacts unchanged. They describe exact
  measured candidates, not the current main commit; their known issues and compatibility
  restrictions still apply.
- Read the adjacent Leaf Log contract and device-token implementation to verify the cross-repo
  claims in the upload note. No Leaf Log source, production data or deployed service was changed
  or tested by this review.

This reconciliation changes documentation and a historical source comment. It does not implement
the remaining features, repeat firmware/hardware validation, or certify a release candidate.

Validation: the documentation website built successfully (41 pages); all 44 local links in the
changed Markdown/MDX documents resolve, frontmatter checks pass, all 19 original backlog notes
appear in the review table, and `git diff --check` passes. The temporary Windows docs junction
used for the build was removed and its original tracked placeholder restored.
