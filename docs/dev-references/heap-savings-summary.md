---
title: Leaf heap savings branch report
description: Measured memory improvements, verification, and remaining tests for the Leaf heap savings branch.
---

# Leaf heap savings branch report

On the Leaf 3.2.5, the measured idle free internal heap rose from **40,296 B
to 123,332 B**, a gain of **83,036 B (81.1 KiB)**. The largest free block
grew from **31,732 B to 61,428 B**. The largest gains came from shutting down
idle Wi-Fi, allocating FANET resources only when needed, and removing unused
TinyUSB classes.

The final memory-only 3.2.5 bench build used **86,356 B of linked static RAM**, down
**21,368 B (20.9 KiB)** from the 107,724 B baseline. The later checked-save and shutdown
reliability fixes bring the tested 3.2.5 total to **86,388 B**, still **21,336 B** below
that baseline; no fresh matched idle measurement is claimed for those fixes. The idle endpoint above
is the last matched cold-idle measurement. The subsequent stack monitor
adds 24 B of linked static RAM, and its shutdown autosave adds zero.

**Closeout decision, November 20:** pause further optimization for stable-release work.
The [restart plan](https://leafvario.com/dev-references/backlog/memory-optimization-followup/) preserves the complete audit
inventory, remaining agreed cohorts, stack trial sizes and larger experiments. No task
stack reductions or smaller release TLS buffers are adopted.

## Idle heap progression

These are on-state readings from one 3.2.5 with Bluetooth enabled in its
settings, the flight timer stopped, and the Web App closed. Each row comes
from that batch's bench run. The live readings reflect radio state, linked
RAM, and allocator behavior. Static RAM reductions contribute to the idle
gain and must not be added to it.

| Build or batch | Free internal heap | Linked static RAM |
| --- | ---: | ---: |
| USB bench baseline | 40,296 B | 107,724 B |
| 1 · Web App, diagnostics, and flight startup | 47,772 B | 100,124 B |
| 2 · Streaming route requests | 46,144 B | 100,204 B |
| 3 · Idle Wi-Fi shutdown | 97,480 B | 100,204 B |
| 4 · Runtime and profile picker | 99,304 B | 98,380 B |
| 5 · FANET lifecycle | 111,356 B | 98,308 B |
| 6 · Route and waypoint loading | 111,332 B | 98,332 B |
| 7 · Vario CSV and bus log cleanup | 111,412 B | 98,252 B |
| 8 · Unused TinyUSB classes | 123,340 B | 86,324 B |
| 9 · Flight and logbook cleanup, stock TLS | 123,332 B | 86,332 B |
| 9 · Final 3.2.5 bench with stack monitor and autosave | — | 86,356 B |
| Checked saves and deferred shutdown · tested November 20 | — | 86,388 B |

[Per-batch measurements and provenance](https://leafvario.com/dev-references/heap-savings-log/)

## Workload-specific results

| Batch | Savings and verification |
| --- | --- |
| 1 | After two screenshots and an 8,000 B route request, closing the Web App left **40,060 B more free**. Its largest block grew from 2,292 B to 31,732 B. Screenshot and request cleanup reduced retention by 11,680 B and 8,032 B at their respective checkpoints. The batch also moved heap history and the file picker to on-demand storage, removed 1,200 B from the GPS object, and removed 256 B from the launch detector. Route save and activation and GPX activation passed on hardware. |
| 2 | Streaming route bodies through an SD staging file avoided overlapping full request buffers. Normalizing the editor document in place avoided a second parsed route. An 18,000 B request that previously disconnected completed with HTTP 200. The net peak delta was unmeasured. |
| 3 | Powering down unused Wi-Fi returned **51,336 B** at idle and **33,332 B** after closing the station Web App in the matched test. Saved-network reconnect passed. |
| 4 | Discarding Runtime or leaving Developer mode returned about **22 KiB** from the same recorder. The closed profile picker released 1,820 B of linked storage. The open picker also used fewer String headers. |
| 5 | Stopping FANET returned **11,524–11,676 B** in three ground-tracking cycles. The same radio resources are avoided at idle and released while the Web App owns Wi-Fi. FANET resumed when the active flight needed it. |
| 6 | Route import avoided a copy of the XCTSK payload. Saved waypoints are read one record at a time, and a completed navigation-state parse releases its JSON before loading the source. The route and waypoint regressions passed. Peak savings vary with file size. |
| 7 | Retiring Developer Vario CSV removed **80 B** of linked instrument state and its temporary logging I/O. The bus log now closes on Developer-mode exit. The measured close returned 460 B on the candidate, versus 8 B while the earlier build continued logging. |
| 8 | Removing unused TinyUSB classes reclaimed **11,928 B** of net linked RAM. Idle heap rose by the same amount, and the largest idle block grew by 12,288 B. Serial, SD mass storage, and readback tests passed. |
| 9 | The IGC logger releases header-only strings after writing the header. An isolated hardware heap delta is pending. Closing the flight summary returned **724 B** and closing Logbook returned **440–444 B** on the stock-TLS cleanup build. Both GPS-synced field IGCs passed their checksum checks. |
| 9 · TLS experiment | A 4 KiB transmit limit with 16 KiB receive reduced TLS context setup by **12,288 B** per active context. The 3.2.5 TLS bench image passed the HTTPS firmware availability check and uploaded eight IGCs. The repeat charging cycle skipped the recorded deliveries. Release builds use stock TLS. |

The values above come from different lifetimes or workloads. The snapshots
and per-object measurements describe overlapping improvements, so the idle
before-and-after comparison is the combined result.

## Runtime and stack headroom

The full Runtime recorder now fits in two allocations, 12,800 B and 9,704 B,
and retains a 32 KiB reserve. Starting it from the menu releases background
Wi-Fi when the Web App is closed. The GPS-synced 3.2.5 run recorded 164
seconds with the recorder active, no allocation failures, and at least
64,560 B of sampled internal heap. Its 36,876 B boot low watermark was
already present at the first sample.

| Task | Allocated stack | Lowest unused in the measured workload |
| --- | ---: | ---: |
| Main loop | 12,288 B | 7,684 B in the outdoor run |
| Bluetooth worker | 5,120 B | 2,988 B in the outdoor run |
| FANET TX | 4,096 B | 2,132 B in the outdoor run |
| FANET RX | 4,096 B | 1,960 B in the outdoor run |
| Leaf Log uploader | 12,288 B | 7,288 B across eight charging uploads |

The table above describes the earlier outdoor/upload workloads. Across the broader
archive, the lowest retained unused stacks are loop **7,072 B**, Bluetooth worker
**2,124 B**, FANET TX **2,076 B**, and FANET RX **1,176 B**. Use those lower values
rather than the newer, lighter workload when planning. See the
[future stack trials and coverage](https://leafvario.com/dev-references/backlog/memory-optimization-followup/#stack-reservations-preserve-now-measure-before-a-future-trial).

The lightweight **Stacks** monitor retains minima across task restarts,
uses no capture buffer, and saves on manual stop, normal power-off, or
low-battery shutdown. The 10 KiB loop stack remains a future trial with
2 KiB of potential on-state savings. The uploader stack exists during
charging transfers and returns to the heap when the worker exits.

[Stack monitor instructions](https://leafvario.com/dev-references/runtime-assessment/#monitor-stack-headroom-during-regular-use)

The 3.2.6 release build includes the stack monitor and shutdown autosave and
uses stock TLS. Both app slots verified, and the 3.2.6 rebooted and mounted
its SD card over USB. The heap measurements and shutdown CSV readback in
this report are from the 3.2.5.

## Remaining verification

The [November 19 full-card hardware tests](https://leafvario.com/dev-references/sd-full-test-2026-11-19/) found
empty navigation state, an empty saved-waypoint file, silent IGC failure when
starting on a full card, and false Stack export success. Diagnostic retry and
a writable-card recording control passed. The [checked-save and startup-warning follow-up](https://leafvario.com/dev-references/storage-fix-review-2026-11-19/)
addresses user-file preservation and Stack false success. It adds +16 B static RAM and
+8,624 B flash in the 3.2.8 bench image. IGC in-flight error behavior remains unchanged
by agreement. The [separate shutdown fix](https://leafvario.com/dev-references/shutdown-fix-review-2026-11-19/) moves cleanup
outside dispatch and drains publishers, adding another +16 B static RAM and +628 B flash.
Simulator watchdog/regression tests and replay-active 3.2.8 shutdowns passed. The
[November 20 3.2.5 follow-up](https://leafvario.com/dev-references/shutdown-radio-test-2026-11-20/) also passed live FANET
plus phone-connected shutdown and Web App lifecycle checks. Its raw IGC checksums match,
but strict validation rejects the known pilot-name Unicode issue. These tests did not
compare main against the candidate.

- The maintainer reported successful 3.2.8 commissioning, Leaf AP, Wi-Fi setup and Web App
  testing during this thread, and previously verified interrupted Leaf Log uploads. These
  are earlier-candidate results, not exact final-build qualification. Completion-reply loss
  and repeat commissioning retries remain explicit fault cases to preserve.
- November 20 verified upload cancellation and USB SD ownership handoff. A fresh exact-release
  candidate upload/retry and end-to-end OTA update remain useful release checks.
- Test a full HTTPS firmware download before promoting the smaller TLS buffer to release.
- Collect longer flight/stack sessions before changing allocations. Combined peer FANET and phone-connected bench shutdown coverage passed; the historical RX minimum of 1,176 B argues for keeping its current allocation.
- Repeat the outdoor timing workload on a matched build to investigate the 219 ms IGC logging slot. Continue the route stress and SD failure checks in the detailed log.

[Detailed hardware test notes](https://leafvario.com/dev-references/heap-savings-log/)
