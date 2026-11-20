---
title: Memory optimization follow-up and restart plan
description: Deferred stack trials, remaining audit decisions, evidence, and test cohorts after the first heap savings batch.
---

# Memory optimization follow-up and restart plan

**Status:** Deferred for stable-release work, by maintainer direction on November 20, 2026.
**Owner:** Firmware maintainers. **Source branch:** `codex/usb-bench-console`.
**Last hardware candidate:** `8a435a426f5383b0097ee96df40cad47ed4ea873`.
**Scope:** Future planning; no additional stack, SDK, buffering or capacity changes in this closeout.

The first cohort delivered the [measured heap improvements](https://leafvario.com/dev-references/heap-savings-summary/).
Keep its current stack reservations and stock release TLS configuration while preparing a
stable release. Existing “yes” decisions remain agreed directions; their scheduling is
paused. A “maybe”, blank, “defer” or rejected tracker row is not authorization to implement it.

## Where to resume

1. Reconcile current main with the saved inventory and the live review grid. The audit was
   based on an older revision; some proposals were subsequently implemented or superseded.
2. Reproduce a baseline on the chosen hardware, SDK and release-equivalent configuration.
   Capture free/minimum internal heap, largest block, static RAM, task presence and stack minima.
3. Start with the remaining agreed representation and temporary-copy cleanups below. Combine
   related changes into a build, but record each change and distinguish measured gains from estimates.
4. Treat stack reservations, SDK pools, file buffering and behavioral compromises as separate
   experiments. Keep a known-good image and compare identical workloads before adopting them.

Use the [USB console](https://leafvario.com/dev-references/usb-bench-console/), [Runtime/Stacks instructions](https://leafvario.com/dev-references/runtime-assessment/)
and [per-batch savings log](https://leafvario.com/dev-references/heap-savings-log/). Archive raw results under
`utils/bench_baselines/` or `test-data/field-testing/`, with commit, image hash, build flags,
hardware, settings, workload and restoration notes. Console support remains opt-in;
the ordinary release build does not enable it.

### Saved decision inventory

The [November 20 inventory snapshot](https://github.com/DangerMonkeys/leaf/blob/main/utils/bench_baselines/memory-followup-2026-11-20.json)
preserves all **422** review rows: **355 decisions** (132 yes, 28 maybe, 5 defer, 2 no,
188 undecided) and 67 reference rows. It includes priorities, phases, historical scoped
estimates and detailed notes for key deferred choices. Its original audit revision is
`e4ce6a5dcc6b0489eba8c3199cc018aede7690d2`.

The snapshot marks 37 IDs explicitly covered by the measured batch log and identifies
the experimental TLS item separately. This is an evidence cross-reference, not an exhaustive
completion audit. Other yes rows require source review before being called outstanding:
they may overlap completed work or have landed elsewhere. Re-read the live item before
resuming; the snapshot does not overwrite later decisions.

Do not sum inventory byte estimates. Some describe alternative layouts, overlapping
lifetimes, temporary peak allocations, or flash rather than internal RAM. In particular,
UI-FLASH-01's large font estimate is flash, not a comparable heap reward.

## First reconciliation targets: remaining agreed cleanup

These are grouped in proposed implementation/test order, preserving the tracker's “yes”
choices. This order is a restart recommendation, not a claim that each item remains absent.

| Cohort | Audit IDs and direction | Reward and tests |
| --- | --- | --- |
| BLE/FANET copies and queues | N12a/N12b: compact tracking events and reject other packet types before BLE queueing; UI-10/UI-11: snapshot only displayed neighbors and numeric statistics; N17a/N18a/N24 and S03: remove redundant copies/state and pack observations | Historical estimates include 880 B for N12a, 1,420 B of UI snapshot storage and 256 B for N17a; they cover different memory lifetimes. Compare maps and task minima, then test phone notifications, peer identities/positions, queue saturation, neighbor churn, Web App stop/restart and shutdown. Keep producer quiescence and queue-slot ownership intact. |
| Instrument and navigation representation | I03, I05–I18, I20a/I20b, I21a/I21b; I31b | Reconcile wind-plot retirement and unused caches first; preserve sensor precision, launch windows and navigation behavior. Combine proven dead fields, padding and borrowed temporary values. Run vario continuity, thermal lifecycle, route guidance/optimizer and parser regressions; check target layouts and active navigation on hardware. I04 is already covered by complete Developer Vario CSV retirement, not the original float-cache proposal. I08b was rejected; do not revive it. |
| Small UI, logging and system cleanups | Remaining yes rows in UI, S04–S10, S24, STORAGE-NT, SYS-02/07/09/11/13/14/16/18/21/22/23/24 | Use the snapshot's exact row choices rather than approving entire prefixes. Examples are borrowed static strings, compact diagnostic IDs, bounded metadata and const-reference APIs. Reconcile completed summary/picker cleanup before editing. Test JSON/CSV compatibility, dialogs, profiles, self-test and sensor startup; validate the transaction bound before reducing Wire buffers. |
| Network response and credential lifetimes | N01a/N01b, N08a/N08c/N08d, N09/N10, N22a, N23a/N23b, LIB-031/LIB-054 | Measure remaining retained buffers and response peaks. Preserve HTTP/chunk decoding, OTA version selection, pairing/upload retry, credential ordering and commissioning retries. Resolve dependency duplication without silently changing library versions. No fixed heap promise until measured. |
| FANET diagnostic queue ownership | S15, alongside existing N15a/N16 lifecycle work | The old queue estimate is 2,304 B; verify how much existing teardown already releases. Change only if a residual allocation is demonstrated. Require RX to park and admitted publishers to drain before freeing any queue. |

N17a deserves a careful ownership check: `fanet_neighbors.h` currently copies the library
neighbor vector and builds a bounded `seen` set. Avoiding the vector copy could reduce
RX stack demand, but a borrowed view must remain valid throughout iteration and mutation.
Do not turn this into an unreviewed concurrent-reference change.

## Stack reservations: preserve now, measure before a future trial

Watermarks are observed minima, not a proof of all possible call paths. Sizes are explicit
firmware/task configuration, not compiler-generated recommendations. Retain guard/watchdog
behavior and a conservative margin; stack exhaustion can corrupt memory or reset the device.

| Task | Current reservation | Lowest unused in retained evidence | Future candidate, not implemented |
| --- | ---: | ---: | --- |
| Arduino loop (SYS-STACK-a) | 12,288 B | 7,072 B in batch-06 route workload | Try 10,240 B first: saves 2,048 B, with projected 5,024 B unused under that observed workload. Keep the old 8 KiB proposal as a later separate experiment. |
| Leaf Bluetooth worker (N13a) | 5,120 B | 2,124 B in November 14 field data | A 4,608 B trial would save 512 B with projected 1,612 B unused under observed use. Validate connected-client, queue and reconnect extremes first. |
| FANET TX (N13c) | 4,096 B | 2,076 B | Keep current size; test packet mix and sustained traffic before proposing a target. |
| FANET RX (N13b) | 4,096 B | 1,176 B in November 14 field data; 1,212 B in November 20 tests | Keep current size. Two live peers do not qualify a crowded network. |
| Leaf Log uploader (N13d) | 12,288 B | 7,288 B across eight charging uploads | Separate charging-only trial after interruption/retry coverage; no persistent on-state saving once the worker exits. |
| NimBLE host task (LIB-008) | Separate SDK task; audit default 4,096 B | Not qualified by the Leaf Bluetooth worker watermark | Instrument and verify actual configuration before proposing any size. |

Loop plus Leaf Bluetooth worker trials could yield **2,560 B (2.5 KiB)** while both tasks
exist. This is potential reservation reduction, not a measured improvement. All these
reductions are paused. Do not use the latest loop-only 7,836 B result in place of the
lower 7,072 B route result.

Collect multi-hour Stacks captures during real flights and combine them with explicit
deep-path tests: large route/import and logbook operations, QR rendering, factory self-test,
Web App/TLS/OTA, failed allocation/storage, reconnects, shutdown and recovery. Record which
tasks actually ran; restart-retained minima prevent a fresh worker from hiding earlier use.
For FANET cover many neighbors, table churn, malformed/long packets, forwarding, coding-rate
changes and concurrent phone traffic. Neighbor entries are bounded storage outside the
worker stack, but processing and temporary table copies still consume stack.

## Larger experiments after the stable release

| Item | Preserved choice and scope | Evidence needed to reopen |
| --- | --- | --- |
| LIB-001: 4 KiB outgoing TLS, 16 KiB incoming | Yes; bench experiment only. Measured 12,288 B less per live TLS context. Ordinary releases retain stock TLS. | Repeat eight-upload-style tests on current SDK; interrupted upload/retry, full HTTPS firmware download/update, chunked responses, server/certificate compatibility and throughput. The measured saving does not exist while TLS is inactive. |
| LIB-067: MSC endpoint buffer | Deferred. Keep 4,096 B now; compare 1,024/512 B later. Potential 3,584 B static at 512 B. Distinct from the completed unused-class removal (LIB-058–063). | SDK/source consistency, Windows/macOS copies, file hashes, throughput, eject, firmware-drive update and SD handoff. |
| S22a–S22e: stdio buffers | SD optimization remains deferred pending measurement. Some rows are maybe and some undecided. Old 4,096→512 B estimate is 3,584 B per open regular file, not always-on RAM. | Slow/full cards, IGC timing and integrity, throughput, write stalls and reopen/close behavior. Do not add frequent IGC flushes without a demonstrated pilot benefit. |
| LIB-005/006/007: NimBLE roles/pools | Roles are maybe; pool reduction remains undecided. Pool estimate 1,752 B is hypothetical. | Prove no central/observer consumer, test MTU/notification pressure, pairing/reconnect and queue exhaustion; map RAM versus flash before promising gains. |
| S11: auto-start capacity | Deferred. Keep 32 samples; S01 layout cleanup is already implemented. | Establish sample-rate/burst/jitter contract and three-second interpolation history. 16 samples might save 640 B beyond S01; eight failed the existing 250 ms sampling test and are ruled out. Test manual G-load arming, automatic starts and relaunches. |
| S19a: narrow logbook summary reader | Deferred. Estimated 0.8–1.0 KiB lower read peak, no ongoing flight gain. | Reopen only for measured browsing pressure; keep display and validation allowlists synchronized, preserve legacy/malformed/Repair Needed behavior and full-fidelity writes/recovery. |
| N11: lazy BLE message pool | Deferred, after N12a. Old 1,324 B estimate becomes about 440 B incremental with compact slots; no enabled-mode benefit. | Measure post-N12a residual cost and prove stable queue ownership, producer/worker drain, allocation failure and repeated teardown/restart. |
| SYS-06-a: smaller Runtime capture | Maybe. Histograms represent 6,400 B of diagnostic-only payload. Split allocations already fixed the observed start fragmentation failure. | Demonstrate reserve/fragmentation pressure remains; preserve useful timing evidence and version the report schema before removing detail. |
| Display buffers, waypoint limits, diagnostic capacities, core-dump scratch | Undecided compromises in the snapshot, not the next approved batch. | Reconsider only for a concrete measured need; quantify rendering/navigation/support losses. Core-dump diagnostics remain valuable for stability. |

S02 and its old pre-takeoff-history relatives were superseded by removal of that diagnostic
ring in #444; do not count them as future gains. Keep the retired wind plot and Developer
Vario CSV decisions distinct from live climb-history and vario functionality.

## Stability and release follow-up

The [November 20 radio test](https://leafvario.com/dev-references/shutdown-radio-test-2026-11-20/) closes the combined
phone-connected/FANET shutdown gap and exercises menu/Web App lifecycle transitions.
It does not qualify hours of in-flight vario timing or crowded-network operation.

- Investigate sensor/audio stalls under [vario timing robustness](https://leafvario.com/dev-references/backlog/vario-timing-robustness/).
  Nearby heap and stack observations do not establish stack exhaustion as their cause.
  Do not change stack sizes while comparing diagnostic I/O effects.
- Preserve [SD robustness](https://leafvario.com/dev-references/backlog/sd-card-write-robustness/) and
  [other blocking bus subscribers](https://leafvario.com/dev-references/backlog/shutdown-message-bus-lock/) as separate reliability work.
  Checked saves/startup warnings and deferred Power shutdown are implemented; general
  power-loss durability, stale FAT free-count behavior and all blocking subscribers are not solved.
- The known non-ASCII IGC pilot-header issue is on the separate
  `codex/igc-header-text` branch (`00275016`), outside this cohort. Raw checksum matches
  are not strict format passes. Reconcile its current status before release.
- Factory commissioning, Leaf AP, Wi-Fi setup and Web App were reported passing by the
  maintainer on the 3.2.8 during this thread. Interrupted Leaf Log uploads were also reported
  passing. Preserve those results; final-candidate retries/fault cases and OTA delivery remain
  useful release checks rather than grounds to reopen optimization.
- A merge is not firmware publication. Follow [releasing](https://leafvario.com/dev-references/releasing/) and
  [runtime assessment](https://leafvario.com/dev-references/backlog/runtime-resource-assessment/) for reviewed simulator/device reports
  on the exact release candidate, preserving known timing and compatibility limits.

## Closeout verification

The closeout changes documentation and archives the November 20 results; firmware source
is unchanged from the tested shutdown candidate. The Leaf C++ formatter made no changes,
the documentation website built all 63 pages, four firmware-configuration tests passed,
and the radio evidence verifier checked 22 archived file hashes and the local candidate
BIN/ELF hashes. Its expected strict-IGC failure remains disclosed, not counted as a pass.
GitHub must separately pass Hygiene, G-load tests, the docs build and all five release
firmware builds for the exact PR head before merge. Release-only artifact publication is
intentionally skipped for a PR; reviewed release runtime reports remain a publication gate.
