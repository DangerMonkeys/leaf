---
title: SD save protection and startup warnings, November 19, 2026
description: Review of checked user-file saves, Stack export handling, startup storage dialogs, and automated 3.2.8 and simulator tests.
---

# SD save protection and startup warnings

The agreed storage changes are implemented and tested on the plugged-in Leaf 3.2.8 and
in the simulator. Existing files survived full-card failures byte for byte. Web saves
succeeded after making space available and correcting a stale FAT32 free-space hint.
The dialog screenshots are ready for wording and visual review. These are uncommitted
changes on `codex/usb-bench-console`, based on `b1ed6ae1`.

The storage work was subsequently committed as `45e599af`. The
[separate shutdown follow-up](shutdown-fix-review-2026-11-19.md) implements the proposed
Power shutdown redesign below; the rest of this report preserves the storage test session.

## Behavior changes

1. **Protect user files.** Navigation state, saved routes, saved waypoints, profiles and
   Web App profile updates share a checked replacement helper. It stages the replacement,
   checks write length, stream errors, flush and close results, then reopens it and checks
   its length and content checksum before replacing the original. The old file is renamed
   to a backup during promotion, with rollback on failure and recovery on a later read.
   An interrupted saved-route replacement remains discoverable in the file picker.
   Loading persisted navigation at boot no longer rewrites that same file.
2. **Keep a failed Stack export retryable.** It reuses the checked writer. Failure removes
   the incomplete CSV and keeps the capture in RAM as `SAVE`. It clears the capture only
   after verified success. This is a small reuse of the user-file helper, with no new
   recording buffer, task or background retry mechanism.
3. **Warn before flight.** Each transition into the On state checks available space once.
   At **10,000,000 bytes or less**, a popup shows decimal MB free and asks the user to
   clear space before the next flight. Zero free space and unavailable/unreadable storage
   have separate wording. `Continue` dismisses it; holding center supports power-off.
   The existing legal warning still comes first when enabled. No storage polling or new
   popup is added during an active flight.
4. **Make replay shutdown explicit in the test harness.** Stop the host producer, wait
   for it to exit, disable injection and verify the reply before requesting power-off.
   Firmware shutdown behavior remains unchanged pending architectural review below.

These saves require room for the staged replacement alongside the old file. They can
therefore fail safely before the card has literally zero bytes free. Verification and
backup recovery improve software error handling; FAT32 is not a power-loss transaction
system and these tests do not establish protection against card removal during a write.
No periodic IGC flushes, new in-flight warnings, or stack size reductions were added.

## Dialog review

Actual firmware-rendered simulator screens are archived under
`test-data/field-testing/sd-full/2026-11-19-3.2.8-fix/screenshots/`.
`warning-dialogs.png` places the low, full and unavailable screens side by side; native
96 x 192 PNGs are also retained. Wording:

| Condition | Message | Action |
| --- | --- | --- |
| Low | Low space; 8.0 MB free; Clear space before your next flight. | Copy files to a computer, then delete old files. |
| Full | SD card full; 0 MB free; Flight recording needs free space. | Copy files to a computer, then delete old files. |
| Unavailable | SD unavailable; Flight recording needs an SD card. | Turn Leaf off, then check the SD card. |

The threshold is inclusive. Positive space under 0.1 MB displays `<0.1 MB free`, rather
than rounding to zero. The plugged-in unit verified warning order and dismissal; the
simulator supplied the review images, absent-card case and precise threshold boundaries.
The space check took **361–365 microseconds** on this card with a populated free-count
cache. A full FAT scan on a card with an unknown count can take longer; this is not a
worst-case timing bound.

## Verification

| Check | Result |
| --- | --- |
| C++ formatter and 3.2.8 bench firmware build | Passed; both OTA app slots flashed and verified |
| Simulator replacement faults | Open, write, short write, flush, close, verification, backup and promotion failures preserved the old file |
| Simulator recovery/callers | Backup recovery, erase without resurrection, profile/waypoint preservation, route picker and read-only navigation restore passed |
| Simulator Stack failures | Frozen capture survived failures and the same capture saved on retry |
| Simulator warning boundaries | Above threshold, exact threshold, low, tiny, zero, absent and unreadable space passed; no popup during an active flight |
| Related simulator regressions | Logbook I/O, developer logging, Leaf Log sync and display settings passed |
| Hardware zero-space web saves | Route editor/import and profile update returned failure; upload returned failure; existing file activation still worked |
| Hardware zero-space card readback | All six fixture files, including navigation state and saved waypoints, remained byte identical; no zero-byte Stack success |
| Hardware recovery | Route editor/import, profile update, upload and activation all returned HTTP 200; profile and route changes persisted |
| Hardware writable Stack export | Valid 106-byte CSV; 3.420-second capture; loop minimum unused stack 7,644 B |
| Normal shutdown after stopping replay | Returned to charging; no message-bus fatal error in this run |
| Card cleanup | Both original files restored with matching SHA-256; diagnostic settings restored; unit left charging |

The full-card Stack capture was not carried through the later recovery run: a test-setup
reset discarded its RAM. Hardware verified failure retention and a new successful capture;
the simulator verified retry of the same capture. FANET was off/uninstalled and there was
no connected Bluetooth phone. This is not a matched comparison with main, a flight/audio
timing assessment, or release qualification. Earlier IGC controls and commissioning,
AP/Wi-Fi/Web App and interrupted Leaf Log upload tests remain separate evidence.

## Size and heap impact

Compared with the preceding 3.2.8 bench image, linked static RAM changes from **86,588 to
86,604 B (+16 B)** and linked flash from **2,410,760 to 2,419,384 B (+8,624 B)**.
The bench-only startup timing variable accounts for 4 bytes plus linked alignment; the
functional build before that instrumentation added 8 bytes of static RAM. Cold charging
heap changed from 229,484 to 229,468 B, matching the final 16-byte static increase.
No extra retained file buffer is introduced. The writer uses a 128-byte stack buffer for
readback. The Stack save function body grew by 121 bytes of code; this is not a separate
measurement of its complete linked cost because it shares the new helper.

These are reliability costs, not new heap savings. The existing branch's earlier measured
savings remain in the [heap savings summary](heap-savings-summary.md).

## Shutdown needs a separate design decision

The replay shutdown failure is **not demonstrably UDP-only**. Source inspection shows
that a button subscriber can perform blocking shutdown while holding the message-bus
mutex, and the production FANET receive worker can also wait on that mutex while shutdown
waits for workers to park. Live FANET traffic has not reproduced a failure here, but this
is a shared lock dependency worth fixing separately.

The proposed [shutdown backlog item](backlog/shutdown-message-bus-lock.md) describes
requesting shutdown from the callback and executing it after dispatch releases the bus,
while quiescing asynchronous producers outside that lock. It preserves the existing
shutdown deadline and watchdog requirements. Simply extending a timeout is not the
recommended architectural fix. This patch does not implement that redesign.

## Test setup issue: stale FAT32 free-space hint

One recovery attempt overlapped host filler writes with turning the Leaf on. That attempt
was discarded, and all six fixture hashes were rechecked unchanged. After a completed
handoff with 8,388,608 actual free bytes, the firmware still reported full and safely
rejected saves. A read-only FAT scan found **512 free clusters but an FSINFO hint of 0**;
both FAT copies agreed. Windows disk checks reported no damaged files and did not update
that optional hint.

A narrowly scoped metadata operation corrected the primary free-count hint to 512,
without changing FAT entries or file contents. Its subsequent volume flush failed with
Windows error 21 and the device restarted. That interruption is retained in the evidence
and is not counted as a successful shutdown test. Afterward the recovery saves passed.
No firmware filesystem configuration or forced FAT scan was added.

The final read-only scan, after restoring originals, found **519 actual and cached free
clusters (8,503,296 bytes)** with identical FAT copies. The filler files remain, leaving
the unit suitable for reviewing the low-space startup dialog. The initial discrepancy's
precise origin is unresolved; interrupted ownership handoff is a possible contributor,
not an established cause. A future USB-handoff/free-count investigation should reproduce
it before choosing whether to change FatFs cache handling. Both the warning and allocator
currently rely on the SDK's free-count cache, so this limitation affects reported space.

## Exact candidate and evidence

- Build: `leaf_3_2_8_bench`, reported version `0.2.4-b1ed.bench+h3.2.8`.
- Firmware SHA-256: `05630762d029b07d876b4354cfa1f1862f08a56d9ff6d9304af6f8ea6c415ffb`.
- The version text is shared with other local builds; identify this candidate by its hash.
- Device MAC: `7C:4F:AD:3B:AC:B0`.
- Evidence: `test-data/field-testing/sd-full/2026-11-19-3.2.8-fix/` includes source hashes,
  flash/build logs, simulator output, USB/HTTP traces, screenshots, file snapshots, final
  validation and the exact local test drivers. `verify.py` validates preserved files and
  outcomes; `--live` also checks current source/build hashes against `candidate.json`.

Review the warning wording and the separate shutdown proposal before expanding the scope.
