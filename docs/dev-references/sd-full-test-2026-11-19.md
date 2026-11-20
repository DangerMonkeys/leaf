---
title: Full SD card hardware test, November 19, 2026
description: Automated 3.2.8 tests of full-card failure reporting, data preservation, recovery, and diagnostic saves on the heap savings branch.
---

# Full SD card hardware test, November 19, 2026

The USB bench console and Web App exercised the heap savings candidate on a plugged-in
Leaf 3.2.8. The tests found **data preservation and failure-reporting defects**. These
results do not qualify the firmware for release or establish regressions relative to main;
main was not run through the same workload. No firmware behavior fixes were made in this session.

## Candidate and setup

- Source: `codex/usb-bench-console`, `b1ed6ae14d09eb1988d4b515297cfaa05a3f4a63`.
- Only build configuration changed: added `leaf_3_2_8_bench`, enabling the existing console.
  Previously uncommitted documentation/field-data changes remained in the worktree.
- Reported firmware: `0.2.4-b1ed.bench+h3.2.8`.
- Firmware SHA-256: `ca939e56601655d5c630efb66b80daad68b815942aaae79808f6eeceb22ed810`.
- Device MAC: `7C:4F:AD:3B:AC:B0`. Both application slots were flashed and verified.
- FAT32 test volume: serial `01114FC5`, 8,059,551,744 bytes, 16,384-byte allocation units.
- Initial card contents: `routes/active.json` and `waypoints/Ed2Hamilton.cup`, plus temporary
  filler files. Both originals were backed up and hashed before testing.
- Three conditions: **0 bytes free**, **16 MB freed for recovery**, then **16,384 bytes free**
  before starting another recording. The last run ended with 0 bytes free.
- Physical pressure and motion sensors remained active. Stationary, synthetic GN RMC/GGA
  sentences were supplied at 5 Hz through the existing developer UDP interface for IGC tests.
  Wi-Fi reconnects interrupted this feed at times; GPS gaps are not evidence of vario stalls.
- Bluetooth was configured on; BLE worker presence was captured. A connected phone was not
  tested. FANET region was off and the console reported `0 UNINSTALLED`; no FANET traffic was tested.
- Automatic flight stopping was temporarily disabled for stationary tests, then restored.

Raw evidence is under `test-data/field-testing/sd-full/2026-11-19-3.2.8/`.
UTC timestamps in the evidence fall on November 20; the local test date was November 19.

## Results

| Operation | Observed result | Assessment |
| --- | --- | --- |
| Full-card boot, menus, GPS handling, Web App and reads | Unit operated, loaded the original CUP's 26 points, and served requests. No general card-full dialog appeared. | Functional responsiveness observed; audio accuracy and flight timing were not qualified. |
| 18,000-byte editor request | HTTP 500, `Could not stage route request on SD card.` Recovery retry returned 200. | Expected error and recovery. Existing saved route remained intact in the second pass. |
| Small route import | First pass returned 200 and left a valid 472-byte route. Second pass returned staging error 500. | First success was real. Full-card operations can free/reuse allocation units, so not every operation must fail. |
| Web profile save | Returned 500 while exhausted; existing fixture survived the second pass. Recovery returned 200 and valid JSON. | Expected error and preservation for the tested Web App path. |
| On-device profile selection | Changed `sd-p1` to `sd-p2`; the 251-byte profile JSON was valid afterward. | Passed this case. Direct rewrite can reuse the old file's allocation; broader interruption guarantees remain untested. |
| CUP upload | Returned 400, `Unable to write waypoint file.` Recovery saved and loaded all 26 points. Existing CUP hash never changed. | Expected error, preservation and recovery. |
| Navigation file loading/selection | Reported loaded successfully, but `routes/active.json` changed from valid 213-byte JSON to **0 bytes** in both exhausted-card passes. | **Failed persistent-state preservation.** Successful RAM loading does not prove selection was saved. |
| Save Point | First full-card attempt left **0-byte `user_waypoints.json`**. With an existing fixture in the second pass, a valid two-point file survived. | **Failed first-pass save integrity.** Later success does not resolve the unchecked-write defect. |
| Recording started on full card | GPS-enabled 1m40s flight and short shutdown flight produced **empty IGC files**. Manual summary showed `Logbook save failed`. | **Recording failure lacks a timely dedicated warning.** Timer/start behavior does not prove recording success. |
| Logbook finalization | Failed saves were reported in summary and USB diagnostics (`saved=0`, ENOSPC). Zero-length main/temp files remained. | Failure notification worked. Empty placeholders remain unusable records, not recovered flight data. |
| Recovery flight | 1,817-byte IGC, closing G record, validator `STATUS_IGC_PASSED`; complete, valid 1,516-byte logbook JSON. | Passed short writable-card control. |
| Card filled during recording | 4m07s flight produced a valid 15,906-byte IGC with 197 B records and a valid closing G record. Summary reported logbook save failure. | Existing IGC allocation was sufficient. **Extension of the IGC beyond its allocated cluster was not reached.** |
| Stack, diagnostics folder absent | Failed export showed SAVE. A later no-reboot retry with free space produced a valid 110-byte CSV. | Passed this failure/retry case. |
| Stack, diagnostics folder present | USB printed `Stack monitor saved /diagnostics/stack_2.csv`, UI returned to START, but file readback was **0 bytes**. | **Failed: false success and loss of the in-memory capture.** |
| Runtime, initial full-card capture | Failed export retained SAVE. Retry after freeing space produced valid 66,314-byte JSON. | Passed tested failure/retry path. |
| Runtime during combined workload | Refused capture with ERR at 52,932 bytes free. Recorder is 22,504 bytes and requires 32,768 bytes reserve: threshold 55,272 bytes. | Expected protective refusal; not an SD-export result or an allocation crash. |
| Bus logger | Started/stopped through real button handling; resulting file was 16,384 bytes at final readback. Unit continued operating. | Storage-boundary workload exercised. Complete bus-log content/error notification was not qualified. |
| Shutdown with GPS replay active | Printed `FATAL ERROR: Lock acquisition failed in MessageBus::receive`, then restarted. | **Failed concurrent replay/shutdown case.** Exact contribution of SD fullness is not isolated. |
| Shutdown with replay stopped | Returned to charging without reboot; boot-wide heap minimum remained unchanged. Pending diagnostics survived and were retried successfully. | Passed control. |

The lowest reported internal heap watermark was **18,864 bytes** during the combined
workload. Runtime's guard rejected another capture rather than using its required reserve.
Radio, Web App, logging and capture states varied, so these readings are not an idle baseline
or an additional heap saving. This workload does not justify reducing task stacks.

## Most useful fixes

1. **Preserve navigation state and saved waypoints on failed replacement.** Check complete
   writes and close/flush outcomes, verify staged content, then promote with rollback. The
   navigation helper currently accepts a nonzero serialized byte count, removes the old
   selection, and renames the candidate without verifying its completed contents.
2. **Correct Stack export success.** The current writer checks individual write return counts,
   calls unchecked `flush()`/`close()`, then clears the RAM session. Require a verified,
   complete CSV before reporting success or discarding the capture. Keep SAVE on failure.
3. **Surface IGC recording failure while flying.** Opening a file is insufficient. Latch short
   writes/sync failures and make recording status truthful without blocking vario processing.
4. **Quiesce injected messages before long shutdown handlers.** Button handling runs on the
   message bus; shutdown can hold it longer than the 3-second wait allowed to another publisher.
   This is a plausible explanation for the replay-only fatal error, not a proved SD-only cause.

Relevant code: `navigation/gpx.cpp` (`writeNavState`, `loadPersistedState`),
`navigation/user_waypoints.cpp` (`writeDocument`), `diagnostics/heap_monitor.cpp`
(`saveStackMonitor`), `logbook/flight.cpp`, `logbook/igc.cpp`, and `dispatch/message_bus.h`.
Several unchecked write helpers also exist in main; use a matched baseline before attributing
these failures to memory optimization.

## Limits and remaining tests

- Repeat failure cases after fixes; specifically force a running IGC past an allocation boundary.
- Repeat Stack export with a preexisting diagnostics folder and full card; verify retry retains RAM.
- Leaf Log upload/status persistence was not tested: this unit returned `linked:false`.
- FANET peers, a connected Bluetooth client, audible vario performance, and outdoor flight remain
  separate tests. Console readiness and sensor output are not substitutes for those checks.
- Diagnostic append logs with existing files, commissioning/self-test report writes, firmware-update
  status writes, physical card removal, reset during replacement, and power loss were not qualified.
- Replacement of a previously valid in-flight logbook placeholder under ENOSPC was not isolated;
  this run's exhausted-card flights failed to create a usable placeholder in the first place.
- The bench build keeps its console available across SD handoff; production USB/power behavior
  still needs the normal release-build checks.

## End state and evidence

The Leaf was left **off on its charging screen**, running the tested console-enabled firmware.
Developer mode, replay, diagnostic switches, and automatic flight stopping were restored to their
original settings. Test outputs were copied into the archive and removed from the card. Both
original files were restored and their SHA-256 hashes verified. The test filler was extended back
to **0 free bytes**. No firmware fixes, commit, or push were performed.

Key evidence:

- `candidate.json`, `sd-before.json`, `23-full-results-card.json`, `63-near-results-card.json`,
  and `65-final-full-card.json`: image identity, card capacity and file hashes.
- `20-flight-manual-stop.png`, `58-near-flight-finish.png`: visible save-failure summaries.
- `21-flight-shutdown.serial.txt`: fatal error during replay/shutdown.
- `26-control-shutdown.json`, `34-diagnostic-retry.serial.txt`: no-reboot control and retry.
- `57-near-diagnostics.serial.txt`, `57-near-diagnostics.png`, and the empty archived
  `card-captures/63-near-results/diagnostics/stack_2.csv`: false Stack success.
- `validation.json`: checksum validation, JSON/CSV checks and preserved-original assertions.
- `harness/`: exact local test drivers and request fixtures used for this run. They are
  evidence/replay aids with machine-specific paths, not an unattended release qualification suite.
