---
title: Shutdown validation (2026-10-29)
description: Verified shutdown recovery on Leaf 3.2.6, exact candidate provenance, and remaining test coverage.
---

The core reproduced USB-recovery failure passes the trials below. This is a code
review and broader testing checkpoint, not firmware release acceptance. The
[design and fault-testing guide](bounded-shutdown.md) describes the implementation.
Route optimization is outside this branch.

## Candidate and build verification

- Branch: `codex/bounded-shutdown`, based on main `07140964`.
- Hardware-tested source: `bebbbf8faf4e5de082ae31d728aa65d13b5ea36c`.
- Device firmware: `0.2.4-beb.shutdown+h3.2.6`; subsequent changes are documentation only.
- Device: Leaf 3.2.6. Both OTA app slots were flashed and hash-verified.

| Build | BIN bytes | Static RAM | BIN SHA-256 |
| --- | ---: | ---: | --- |
| `leaf_3_2_6_release` | 2,416,992 | 107,404 B | `3aaa63a564111efde85308c67742d299fcd9c8d8d8a21f5707608a72ae9ec395` |
| `leaf_3_2_6_shutdown` | 2,417,632 | 107,420 B | `28cb311f131811bf16ece6b485455e42bd9e8ad68a7577b27c76f24a59ce2b50` |

Formatter/whitespace checks and both clean-source native builds passed. These
static sizes are total build usage, not added heap cost. No task, stack, or
schedule slot was added; actual burdened CPU/heap overhead remains unmeasured.

The simulator build, shutdown policy/integration/menu, developer logging, runtime
capture, and runtime lifecycle suites passed. Seventeen shutdown scenarios cover
normal and disabled injection, staged faults, nested cleanup, long hold with the
main loop unserviced, both backstop modes, and held-button recovery. Fresh fixture
directories prevent earlier saved flights from masking a failed save. Rendered
menu labels were checked with and without FANET.

The shared RTC policy model verifies no register writes/feed/rearm when an active
guard trips, expiry at the original ten-second deadline, unarmed emergency
fallback, safe configuration order, escalation, and full disarm. Integration
checks release the modeled latch at 8,004 ms, or act at 10,004 ms for RTC modes;
the phase offset and physical reboot timing are not hardware measurements.

Both native ELFs place emergency functions and `esp_timer_get_time()` in IRAM.
Audited emergency relocations use internal/RTC state and registers, without
locking atomic helpers or flash strings. Native worker parking acknowledges
quiescence outside driver locks before task deletion.

## Physical results on the corrected candidate

Timings are approximate maintainer observations. USB inventory and copied SD
records corroborate recovery; GPIO48/current timing was not instrumented.

| Trial | Result | Evidence |
| --- | --- | --- |
| CLR fault, USB, center released | Returned to charging | `other_watchdog` reset; `power-latch`, reason 1 recovery |
| BLE fault, Bluetooth disabled | Charging after about twelve seconds | `ble-deinit`, reason 1 recovery |
| BLE fault, Bluetooth advertising, no client | Returned to charging | Bluetooth start recorded before test; `ble-deinit`, reason 1 recovery |
| BLE fault, center held through reset | Stayed in charging; fresh On after release | Maintainer observation and subsequent `ble-deinit`, reason 1 recovery |
| CLR fault, battery only | Power-off followed by fresh stable On | Maintainer observation; subsequent POWERON boot and Bluetooth restart |
| Normal OFF, Bluetooth advertising, active flight | Prompt normal charging; complete summary saved | NimBLE teardown completed; no new boot/reset or recovery event |

The normal OFF trial finalized a **17-second**, revision-2 summary marked
`complete: true` on this exact firmware. GPS time was unsynchronized and
`track.saved` was false, so no IGC was created. Existing logging deliberately
waits for valid GPS date/time before opening a track. This verifies summary
finalization, not IGC finalization. An earlier candidate saved a checksum-valid
IGC, but that is not substituted for current-candidate track coverage.

Application USB and SD access returned after recovery; both existing test routes
retained their hashes. Actual Bluetooth teardown is supported by
`ble-end-nimble-after` and `ble-end-after` on the normal path, rather than inferred
from a deliberately injected BLE stall.

## Reason for preserving the armed watchdog

Earlier candidates sometimes stayed blank with no USB/SD interface after a
shortened emergency watchdog reset. Differential testing recovered before display
clear (SLP), failed after clear (CLR), and recovered at the later BLE boundary
when only the original independent timer expired (RTCB). The corrected candidate
leaves that original backstop untouched at the eight-second latch deadline;
CLR and BLE recovery then passed. The precise prior failure mechanism remains
unproven. A separate legacy fatal log and an unattributed core dump were not
claimed as evidence of this failure's cause.

## Evidence retention and remaining coverage

Raw CSVs, SD/flight copies, exact BIN/ELF pairs, build/test/IRAM audit output, and
flash records are retained locally under ignored `sim/build/hardware-evidence/`.
The final candidate directory is
`shutdown-recovery-bebbbf8f-20261029-194137/`; its manifests and per-trial JSON
records identify files and SHA-256-verified copies. Full investigation documents
are retained locally under ignored `sim/build/review-preparation/original-docs-f5cc69e2/`
and in this branch's earlier history. Raw flight data and generated logs are not
part of the PR.

Before release, complete coverage for:

- Bluetooth with a connected client and repeated stop/start cycles, plus FANET activity.
- GPS-synchronized IGC finalization on this exact candidate and representative long/slow-SD saves.
- Native emergency long hold with the main loop blocked outside an armed shutdown.
- Burdened runtime/heap overhead, additional hardware variants, and instrumented cutoff timing.

Follow the [runtime assessment](runtime-assessment.md) and
[release requirements](releasing.md) for a published firmware candidate. Those
release reports are not a prerequisite for submitting this PR.
