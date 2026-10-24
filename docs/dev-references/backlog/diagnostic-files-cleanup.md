---
title: Diagnostic Files Cleanup Backlog
description: Current developer diagnostic files and remaining cleanup work.
---

# Diagnostic Files Cleanup Backlog

Status reconciled **2026-10-24**: diagnostic categories and their Developer controls are
implemented. Separate dev firmware builds were retired in `c840faf9` (#420); runtime Developer
mode and each category's setting control logging in the release image.

## Current files and controls

| File under `/diagnostics` | Control and contents |
| --- | --- |
| `system_events.csv` | System Events (`diag_systemEvents`): lifecycle/boot events, heap checkpoints, buffered heap dumps, registered-task stack headroom and Leaf Log retry details |
| `network_events.csv` | Network Events (`diag_networkEvents`): WiFi setup and saved-network connection events |
| `web_requests.csv` | Web Requests (`diag_webRequests`): web request timing and session/counter diagnostics |
| `vario.csv` | Vario (`diag_vario`): instrument/filter diagnostics |
| `runtime_N.json` | CPU/timing capture (`diag_cpuUtilization`): explicit export of a bounded, frozen RAM capture |

The former `heap_lifecycle.csv`, `heap_monitor.csv`, `wifi_setup.csv`,
`wifi_autoconnect.csv`, `webapp_requests.csv` and `boot_report.csv` names describe older
captures, not separate current writers. Their lifecycle/network/web information is consolidated
into the category files above. `CPU_UTILIZATION_PATH` remains a legacy constant; the runtime
assessor no longer writes synchronous per-second `cpu_utilization.csv` rows.

All categories require `dev_mode` as well as their category toggle. The web app Developer card
can change these controls. Diagnostic filesystem writes require firmware SD ownership and are
paused while USB owns the card. Runtime capture freezes before explicit export; it does not
stream timing rows to SD in the measured path. See [Runtime assessment](../runtime-assessment.md)
for capture allocation, export, comparison and simulator limitations.
The runtime capture also has an implemented download endpoint, `/api/debug/runtime/download`,
after freeze/export; a general CSV file-management UI is separate work.

## Flight diagnostics in IGC files

The `/diagnostics` categories above are separate from manufacturer L records added to normal
IGC tracks in #431 and #432. G-load interval diagnostics are written once per second while the
IGC is open, with a final group at stop. Launch diagnostics retain up to thirty one-second
pre-start inputs and their scores; start/end records identify the timer cause. These records
use the normal checksum-aware IGC writer and do not require Developer mode or a CSV toggle.
No IGC diagnostics are available when track saving is disabled or the flight ends before its
IGC opens. See [Flights and Logging](../flights_and_logging.md).

These temporary field-tuning records add SD traffic to the flight workload. Measure their
effect in exact-candidate runtime reports and decide what to retain or gate after validation.

## Remaining work

- Add a focused UI for downloading/clearing diagnostic CSVs if field use warrants it; category
  toggles exist and should not be reimplemented.
- Consider rotation/truncation for append-only CSVs and retention limits for numbered runtime
  exports to avoid unbounded SD growth across development sessions.
- Reassess which diagnostics to retain after release hardening, preserving checks needed for
  correct behavior.
- Coordinate checked writes and failure-path reporting with
  [SD card write robustness](sd-card-write-robustness.md); a diagnostic write must not recurse
  into more writes on a failing card.
