---
title: SD Card Write Robustness Backlog
description: Two-phase plan for recoverable logbook saves and resource-conscious SD handling throughout Leaf firmware.
---

# SD Card Write Robustness Backlog

Status: proposed; investigation completed on 2026-10-21. This document records findings and
recommendations only. No firmware changes or hardware verification were performed for this review.

## Goal and phase boundaries

Prevent a failed or interrupted save from unnecessarily destroying a valid flight record, preserve
recoverable data, and accurately report storage failures without materially increasing ESP32 memory
use or disrupting instrument processing.

Keep FAT32 and the existing JSON/IGC files. Deliver this work in two phases:

| Phase | Scope | Intended result |
| --- | --- | --- |
| 1: focused logbook hardening | Checked logbook replacement, conservative recovery, retained finalizations, and targeted failure tests | Address the demonstrated loss windows with small changes around existing save milestones |
| 2: firmware-wide refinement | Shared storage lifecycle, all file writers, continuous logging, diagnostics, and broader power-failure validation | Apply consistent storage guarantees throughout the firmware and measure their resource costs |

Phase 1 should be independently releasable. Avoid making it depend on a new storage task, a global
filesystem abstraction, a JSON schema migration, or a persistent internal-flash journal.

## Incident evidence

The review used the supplied `write-failures.md` and `bethany_sdcard.img.zip`, plus the repository
code at `v0.2.3` and the current code available during the investigation. The writeup's suggestions
were evaluated as evidence and proposals. The image was inspected read-only; no filesystem repair
was run. Keep the original image and recovered user data outside the repository.

- Affected file: `/LOGBOOK/2026-10-18_11-50-25_cc1c2348.json`.
- Its directory entry has size zero and starting cluster zero.
- All 86 other JSON files parse successfully. Of those, 83 are other logbook records; the remaining
  three are the user-waypoint, route, and profile files.
- Three complete JSON payloads for flight `cc1c2348` remain in unreferenced clusters:

| Cluster | Record | Duration | FAT state |
| --- | --- | --- | --- |
| 18783 | Initial placeholder | 0 seconds | Free in both FAT copies |
| 18785 | First-fix update | 2 seconds | Free in both FAT copies |
| 18844 | Final flight record | 4373 seconds, or 1h 12m 53s | Free in FAT 1; allocated in FAT 2 |

- Cluster 18844 is the only differing entry between the two FAT copies. FAT mirroring is enabled.
- The recovered final record identifies firmware `0.2.3+h3.2.7`, starts at local time
  `2026-10-18T11:50:25`, ends at `2026-10-18T13:03:18`, and references
  `tracks/2026-10-18-XLF-000-01.igc`.
- That IGC is 240853 bytes and contains 4373 B records, from UTC `18:50:26` through `20:03:18`.
  It has no G record, which is expected: IgcLogger `v0.0.5`, used by Leaf `v0.2.3`, deliberately
  emitted none. The absence is not evidence of another interrupted write, and this review does
  not establish cryptographic validity or flight-service acceptance.
- The card is FAT32 with 4096-byte allocation units and two FATs. It was nowhere near full.
- No visible `.tmp` or `.bak` files remained to recover through normal filenames.

The evidence strongly suggests an interrupted or failed filesystem metadata update: the final JSON
payload reached the card, but its directory linkage and FAT allocation did not remain consistent.
It cannot identify the exact trigger or prove the card defective. Power loss, reset, an SD write
failure, or interference from another filesystem writer remain possibilities.

Recovering unreferenced cluster contents requires offline analysis. Filename-based recovery alone
cannot recover this existing incident from the visible zero-byte file, nor guarantee that all future
FAT corruption is repairable.

## Current weaknesses and implementation boundaries

| Area | Observed behavior | Consequence |
| --- | --- | --- |
| `src/vario/logbook/logbook_entry.cpp`, `writeJson()` | Serializes to `.tmp` without checking the result, closes, removes the old JSON, then renames | Incomplete data may replace a valid record; deleting first creates a loss window |
| `src/vario/logging/log.cpp`, `flightTimer_stop()` | Ignores `finalize()`'s result and resets the entry and statistics | A failed completed-flight save loses its in-memory retry source |
| `src/vario/logbook/logbook_store.cpp`, `recoverAtomicResult()` | Trusts destination existence, deletes backups without validating it, and removes temporary files | An empty destination can cause a valid recovery candidate to be destroyed |
| `logbook_store.cpp`, `writeLeafLogResult()` | Separate replacement routine; accepts any nonzero serialization result and eagerly removes recovery files | Upload-status updates can reintroduce partial-write and recovery risks |
| `src/vario/logbook/flight.cpp` and `igc.cpp` | Continuous track writing has no explicit periodic synchronization; final close exposes no outcome | Track durability and actual save status are difficult to assess |
| `src/vario/power.cpp` | Stops the flight before power-down but cannot act on the ignored save result | Shutdown cannot reliably report or retry a failed finalization |
| Other writers | Profiles, routes, waypoints, web handlers, and diagnostics use different save strategies | Fixes confined to flight JSON leave similar weaknesses elsewhere |

These flight-save weaknesses were present in `v0.2.3` and remained in the current code reviewed.
Current `storage/sd_card.*` already implements firmware/USB ownership states and handoffs; extend
and audit that design in phase 2 rather than introducing another ownership mechanism. Its presence
does not establish that USB caused this incident or that every caller already observes it correctly.

### Synchronization details that affect the design

The installed Arduino ESP32 3.1.0 framework uses ESP-IDF 5.3.2. Its `File::flush()` implementation
calls `fflush()` followed by `fsync()`, but returns no status and ignores both results. Its close
wrapper similarly hides `fclose()` errors. ESP-IDF forwards file close to FatFs `f_close()`, which
already synchronizes the file.

Therefore, adding an unchecked `flush()` before close is not a complete fix. A small helper using
the existing C/POSIX interface can expose write, sync, and close outcomes for important saves without
replacing the filesystem library. Keep one buffering/handle model per file; do not mix independent
Arduino and C handles for the same write.

FatFs rename fails when the destination exists. Removing the explicit delete and assuming POSIX
overwrite-rename semantics will not work. The replacement sequence must remain recoverable across
multiple metadata operations. Even successful sync and readback cannot prove survival of a later
power interruption or SD-controller failure.

## Phase 1: simplest changes with the highest impact

### 1. Share one checked logbook replacement routine

Use the same narrow routine for placeholders, first-fix/time updates, completed flights, and Leaf Log
upload/rejection status updates. Leave unrelated storage callers for phase 2.

Proposed sequence:

1. Require firmware filesystem ownership and resolve an earlier incomplete transaction before
   reusing its temporary or backup filename. Do not truncate the only valid candidate.
2. Build the JSON document and reject allocation/overflow failures. Measure the expected serialized
   length and check the exact written count, including any trailing newline.
3. Write a temporary file in the same directory through buffered output. Check write errors,
   `fflush()`, `fsync()`, and close, preserving the original failure if cleanup also fails.
4. Reopen and validate the temporary file: expected length, complete JSON with no unexpected
   trailing content, supported schema, matching flight identity, and required fields for the
   intended placeholder or completed record. Parsing successfully as `{}` is insufficient.
5. Preserve the previous valid main file as `.bak`, then promote the verified `.tmp` to the main
   name. Check each operation. A failed promotion must retain the valid temporary and backup
   candidates; rollback must also be checked.
6. Confirm the promoted path is readable and valid before treating the save as successful. Retain
   a bounded previous valid copy; remove an older backup only when another validated copy exists.
   Define backup rotation and cleanup explicitly rather than deleting every backup eagerly.

Keep the existing JSON fields and filenames in phase 1. Use stream serialization and reuse a single
JSON document for readback, avoiding two simultaneous full documents or a serialized `String` copy.
Bound validation size and nesting according to supported records without rejecting legitimate
existing files or discarding fields during upload-status updates.

### 2. Replace destructive recovery with conservative candidate validation

- Validate main, `.tmp`, and `.bak` independently. Existence alone is never a validity test.
- Recover a valid replacement when the main file is missing, empty, or malformed. Prefer a
  completed record over an earlier placeholder for the same flight when the evidence is clear.
- Preserve conflicting valid completed records when their ordering cannot be established. Do not
  guess from FAT timestamps or delete the ambiguous candidate; expose an unresolved recovery state.
- Keep a valid main record when it is clearly authoritative, and clean up alternatives only after
  validation establishes they are redundant. Never delete the only valid copy.
- Discover candidate sets even when only `.tmp` or `.bak` exists. Normal `.json` enumeration alone
  will miss them. Run a bounded recovery pass before normal logbook enumeration and Leaf Log scans,
  and before overwriting candidates for the same flight.
- Process one candidate set at a time. Do not mutate directory entries under a live iterator in a
  way that skips entries; close/resume safely or use an explicitly bounded work list.
- Serialize recovery against saves and readers. Opening a summary must not remove an active
  writer's temporary file.
- Make intentional deletion remove associated candidates and cancel pending retries so a deleted
  flight cannot reappear during recovery. Treat deletion differently from failed replacement.

This is a small directory/candidate recovery operation, not an on-device FAT checker or a scan of
the card's unreferenced sectors. Keep the original image as an offline regression reference; use
small synthetic fixtures in automated tests instead of checking in user data.

### 3. Preserve failed finalizations and report them accurately

- Check `finalize()` and retain a bounded completed-flight snapshot until save succeeds. Capture
  statistics, flight ID/path, pilot/glider context, track association/status, clock validity, and
  the actual ending time once. A retry must not move the landing time forward.
- Keep pending completed data separate from the next active flight. Use bounded retries with
  backoff instead of a tight loop, including a bounded retry opportunity during ordinary shutdown.
- Define pending capacity and full-capacity behavior explicitly. A later flight must not silently
  overwrite an unsaved one. RAM retention handles transient failures, not power loss.
- Show separate track and logbook outcomes. For example, show `Track saved; logbook save failed`
  only when track success is known; otherwise report the uncertainty. Do not play/display an
  unconditional save-success indication after failure.
- Record the IGC path in the existing first-fix update once the track opens, rather than waiting
  until finalization. Preserve that association in subsequent placeholder/time updates. Reuse the
  existing save milestone instead of adding frequent writes.
- Report a compact failure stage and error code to existing diagnostics/serial output. Storage
  failure reporting must not recursively attempt more writes to the failing card.

### 4. Verify the focused change before release

Add meaningful fault-injection tests for the shared routine and its callers:

- Short/zero writes, full storage, JSON allocation failure, and sync/close failure.
- Failed backup/promotion/rollback operations and interruption between every replacement step.
- Zero-byte or malformed main files with a valid temporary or backup; temporary-only and
  backup-only candidate sets; conflicting valid records; repeated recovery.
- Failed finalization followed by retry, a subsequent flight, shutdown, and intentional deletion.
- Leaf Log status updates retaining the flight content and the same recovery guarantees.
- Existing records without new metadata, valid unsynced/no-GPS flights, and flights without tracks.

The host-directory simulator can test application decisions but does not reproduce FAT metadata
ordering. Include a focused FAT-backed test and controlled hardware interruption checks for this
save sequence before declaring power-failure behavior verified. Follow the repository's firmware
verification workflow when implementing the change.

Phase 1 exit criteria:

- A failed save does not intentionally discard the previous valid record or the pending snapshot.
- Whenever a valid named candidate survives, recovery preserves it and selects it when unambiguous.
  Unresolved ambiguity remains visible and preserves evidence.
- Success requires checked I/O and a validated promoted file. An unrecoverable error remains visible.
- Added work occurs at existing save milestones; no new per-fix sync, whole-card scan, or
  unbounded RAM collection is introduced. Measure peak heap and save latency against a baseline.

## Phase 2: complete refinement across the firmware

### 1. Audit and coordinate every storage user

Inventory all `SD_MMC`, raw SD, and FatFs users, including IGC, bus logs, logbooks, profiles, routes,
GPX imports, user waypoints, web reads/writes/downloads, firmware updates, commissioning/self-test
files, Leaf Log scratch queues, and diagnostics. Classify durable user data separately from
rebuildable scratch data so temporary queues do not incur the same validation cost as flight records.

Extend `storage/sd_card.*` with a small lifecycle/transaction guard that:

- Requires firmware ownership throughout firmware I/O, not just at the initial open.
- Accounts for active transactions and long-lived readers/writers before ownership changes.
- Prevents rename/delete of open files and races between recovery, web requests, and uploads.
- Stops new writes and drains current work before unmount, format, USB handoff, or orderly shutdown.
- Handles removal, failed remount, and reinsertion without reusing stale file handles.

Do not assume per-operation filesystem locks protect a multi-step save transaction. The installed
SDK has `CONFIG_FATFS_FS_LOCK=0`; audit explicit handle ownership and synchronization rather than
assuming duplicate-open/rename protection exists. Prefer a small guard and existing scheduling
unless measurements demonstrate a need for a dedicated storage task.

Coordinate with the [USB ownership backlog](usb-mass-storage-power-on-ownership.md). Firmware must
not access FAT while a USB host owns the medium. USB inactivity/suspend does not establish safe
release, and forcing disconnection cannot flush unsent host caches.

### 2. Introduce one bounded storage-drain sequence

Use a common sequence for shutdown and transitions: reject new work, finish/check pending track
and logbook writes, flush/close other writers, then unmount or hand off ownership as appropriate.
Recheck the boundary before USB presentation and power-latch release. Low-battery shutdown needs a
strict time budget so retrying a bad card cannot indefinitely delay power-off.

Recovery should run before normal writes, including diagnostics, when firmware acquires the card.
Defer opportunistic diagnostic writes during critical commits. Avoid repeated remount/retry storms
after a persistent failure; retain useful state and give the user a clear storage outcome.

### 3. Harden continuous IGC and bus logging

- Propagate short writes, write failures, synchronization errors, and close results from the track
  writer. Having a filename or an open handle is not proof that the track was saved.
- Evaluate periodic checked IGC synchronization, initially around 15-30 seconds, and a final sync
  at flight end. Select the interval from measured latency and acceptable recent-data loss.
- Use buffered/block writes; avoid synchronizing every GPS fix or sensor message.
- Measure the longest blocking SD operation, not just average throughput. Instrument updates share
  execution time with storage work, and an individual card operation may exceed a scheduler budget.
- Give optional high-rate bus/diagnostic logging a separate buffering/sync policy and lower priority
  than preserving flight data. Keep buffers bounded and overflow behavior explicit.

Leave global `CONFIG_FATFS_IMMEDIATE_FSYNC` disabled unless measurements justify changing it; it
increases disk operations for all affected writers rather than only important commit boundaries.

### 4. Apply recoverable saves and stronger ordering consistently

Migrate durable profile, route, waypoint, and related web-handler saves to the phase 1 routine with
appropriate validators. Remove duplicate replacement implementations as they are migrated. Preserve
existing data and public formats, and define recovery/deletion rules for each file family.

Add a small revision counter and explicit completion state for new logbook records if needed to
resolve candidate ordering deterministically. Preserve completed-flight state through later upload
status updates, support older files without these fields, and verify web/export/upload compatibility.
Do not use coarse FAT timestamps as transaction sequence numbers.

Consider a streamed length/checksum mechanism only if testing shows a gap beyond checked writes
and structural validation, such as syntactically valid corruption. Keep it bounded and compatible
with legitimate edits; a checksum detects damage but cannot restore missing data or repair FAT.

### 5. Make diagnostics and resource budgets explicit

Record compact counters and the last failure's stage, code, expected/actual bytes, flight ID, and
reset reason. Keep failure-path diagnostics in bounded RAM/serial output when the card is failing;
persist only when safe and consistent with existing diagnostic settings. Avoid recording complete
flight payloads or location data in routine error messages.

| Mechanism | Memory/CPU budget | SD traffic/storage budget |
| --- | --- | --- |
| Checked JSON commits | One reusable document, small buffered I/O, a few bounded passes at save milestones | One replacement plus validation reads |
| Recovery | One candidate set at a time; sliced directory work | Read recovery candidates, not every historical JSON or every card sector |
| Retained finalization | One bounded snapshot/capacity policy and infrequent retries | Retries only after failures |
| Previous valid copy | Small bookkeeping; no full-file RAM duplicate | Approximately one allocation unit per small backup: 4 KiB on this image, typically 16 KiB with the current formatter |
| Periodic track sync | No unbounded queue; measure worst-case stalls | One sync per selected interval, not per fix |
| Diagnostics | Fixed counters/last-error record | Rate-limited; never recursive on SD failure |

Measure peak heap, largest free block, stack use, flash growth, save duration, longest instrument-loop
stall, synchronization frequency, and write amplification. Test on supported hardware and several
cards, including slow, nearly full, and failing/removal scenarios. Avoid increasing allocation size,
preallocating large files, or changing buffering globally without evidence of a useful tradeoff.

### 6. Expand validation to the complete lifecycle

Exercise power interruption around data writes, sync, close, FAT updates, rename, backup rotation,
and recovery. Include repeated boot/recovery, new flights after failures, manual deletion, card
removal/reinsertion, low-battery shutdown, web access, upload cancellation, firmware updates, and
USB ownership transitions with active host operations.

Use simulator tests for deterministic application logic, a FAT-backed harness for filesystem
behavior, and controlled hardware power cuts for real SD-controller behavior. Inspect resulting
images offline for orphaned clusters, cross-links, and FAT disagreement. A passing host-filesystem
test or successful readback is not proof of physical power-loss durability.

Phase 2 exit criteria: every storage user has an explicit ownership/lifecycle policy, every durable
replacement has defined recovery behavior, failures reach the caller/user appropriately, and measured
resource use stays within an agreed instrument-responsiveness and memory budget.

## Deferred options and limits

- Do not add automatic on-device FAT repair, unreferenced-cluster carving, a full-card boot scan,
  or automatic reformat after a mount/write failure. Preserve evidence and use offline tools for
  suspected structural corruption.
- Do not migrate filesystems or add a general journaling system as the initial response. FAT32
  compatibility with USB mass storage remains useful, and the demonstrated application-level
  weaknesses can be addressed first.
- Do not rewrite JSON every second, checkpoint every sensor update, or journal continuously to
  internal flash. Revisit occasional checkpoints only with a defined recovery need and write budget.
- If stronger completed-flight protection is still required after testing, evaluate a small bounded
  internal-flash pending-finalization record written once per completed flight. It should capture
  enough immutable data to replay the JSON save, survive reboot, and be cleared only after verified
  SD commit. Define power-failure behavior, replay identity, flash wear, and capacity before adoption.
- An IGC-based offline reconstruction tool could recover some summary fields when JSON is lost,
  but cannot reproduce all sensor statistics or user/profile metadata. Clearly label reconstructed
  records and unavailable values.

FAT is not a transactional filesystem. Backups on the same card, checked sync, and readback reduce
avoidable loss windows but cannot guarantee survival of every interrupted metadata update or card
failure. A surviving placeholder is also not equivalent to recovering the completed flight's final
metrics; report the distinction explicitly.

## References and related work

- [FatFs application note: critical sections and failure behavior](https://elm-chan.org/fsw/ff/doc/appnote.html)
- [FatFs synchronization and close behavior](https://elm-chan.org/fsw/ff/doc/sync.html)
- [FatFs rename requirements](https://elm-chan.org/fsw/ff/doc/rename.html)
- [ESP-IDF 5.3 FAT filesystem support and configuration](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/api-reference/storage/fatfs.html)
- [ESP-IDF 5.3.2 VFS implementation](https://github.com/espressif/esp-idf/blob/v5.3.2/components/fatfs/vfs/vfs_fat.c)
- [USB mass-storage ownership backlog](usb-mass-storage-power-on-ownership.md)
- [Leaf Log auto-upload backlog](leaf-log-auto-upload.md)
- [Diagnostic files cleanup backlog](diagnostic-files-cleanup.md)
- [Heap availability improvements backlog](heap-availability-improvements.md)
