---
title: SD Card Write Robustness Backlog
description: Two-phase plan for recoverable logbook saves and resource-conscious SD handling throughout Leaf firmware.
---

# SD Card Write Robustness Backlog

Status reconciled **2026-10-24**: Phase 1 merged to `main` in `605810eb` (#430). Automated
fault-injection and FAT-backed tests passed. Leaf 3.2.8 checks also passed for no-GPS saves,
absent-card failure followed by a new flight, staged candidate recovery, and deletion/cold boot.
The USB diagnostic startup fix passed an initial monitored charge-to-on check. GPS/IGC flights,
active-flight shutdown, target resource measurements and controlled power interruption remain
qualification work; Phase 2 remains unimplemented.

Investigation completed on 2026-10-21; the maintainer refined the scope on 2026-10-22 to automatic
recovery with revision/completion metadata and a short, bounded final-save sequence without
session-long RAM retention. The implementation/verification record below distinguishes completed
checks from remaining qualification; it does not establish physical power-loss durability.

## Goal and phase boundaries

Prevent a failed or interrupted save from unnecessarily destroying a valid flight record, preserve
recoverable data, and accurately report storage failures without materially increasing ESP32 memory
use or disrupting instrument processing.

Keep FAT32 and the existing JSON/IGC files. Deliver this work in two phases:

| Phase | Scope | Intended result |
| --- | --- | --- |
| 1: focused logbook hardening | Checked logbook replacement, revision/completion metadata, automatic recovery, bounded final-save retries, and targeted failure tests | Address the demonstrated loss windows with small changes around existing save milestones |
| 2: firmware-wide refinement | Shared storage lifecycle, all file writers, continuous logging, diagnostics, and broader power-failure validation | Apply consistent storage guarantees throughout the firmware and measure their resource costs |

Phase 1 should be independently releasable. Avoid making it depend on a new storage task, a global
filesystem abstraction, a migration of existing JSON files, or a persistent internal-flash journal.
New optional revision/completion fields are in Phase 1; existing files without them remain supported.
Continuous IGC/bus-log write, synchronization, signing, and close behavior remain unchanged in
Phase 1. Recording the existing track association earlier in JSON is included.

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

### What Phase 1 addresses about this incident

Phase 1 directly addresses the demonstrated application-level loss windows: unchecked replacement,
removing the previous main before promotion, destructive recovery, and ignoring final-save failure.
Checked writes/sync/close/readback, a retained previous valid file, recoverable temporary promotion,
and bounded final-save retries are intended to prevent avoidable loss and recover automatically
when a valid named candidate survives. Earlier track association can preserve the IGC link even
when only a placeholder survives; that is not recovery of the final flight metrics.

The original trigger remains unproven. Phase 1 cannot guarantee prevention of the exact observed
FAT/directory corruption, repair the supplied image, or recover a final payload that survives only
in unreferenced clusters. A same-card backup may also be affected by filesystem or card failure.
If no named completed candidate survives, a preserved placeholder is only partial protection.
Firmware-wide writer/lifecycle auditing remains in Phase 2.

Treat this as targeted mitigation of known weaknesses consistent with the incident, not a verified
root-cause fix. Fault injection must cover an empty/malformed main with surviving candidates;
FAT-backed and controlled hardware interruption tests must establish the new sequence's actual
behavior before claiming power-loss resilience. The incident alone cannot establish a recurrence
rate or quantify the risk reduction.

## Historical weaknesses identified before Phase 1

The following table describes the investigated code before #430. Phase 1 replaces the unchecked
logbook/result saves and destructive recovery, and handles finalization failures through the
bounded save sequence and user feedback. Continuous-track and other-writer guarantees remain
Phase 2 work; these historical rows are not a list of current logbook implementation defects.

| Area | Observed behavior | Consequence |
| --- | --- | --- |
| `src/vario/logbook/logbook_entry.cpp`, `writeJson()` | Serializes to `.tmp` without checking the result, closes, removes the old JSON, then renames | Incomplete data may replace a valid record; deleting first creates a loss window |
| `src/vario/logging/log.cpp`, `flightTimer_stop()` | Ignores `finalize()`'s result and resets the entry and statistics | A failed completed-flight save loses its in-memory retry source |
| `src/vario/logbook/logbook_store.cpp`, `recoverAtomicResult()` | Trusts destination existence, deletes backups without validating it, and removes temporary files | An empty destination can cause a valid recovery candidate to be destroyed |
| `logbook_store.cpp`, `writeLeafLogResult()` | Separate replacement routine; accepts any nonzero serialization result and eagerly removes recovery files | Upload-status updates can reintroduce partial-write and recovery risks |
| `src/vario/logbook/flight.cpp` and `igc.cpp` | Continuous track writing has no explicit periodic synchronization; final close exposes no outcome | Track durability and actual save status are difficult to assess |
| `src/vario/power.cpp` | Stops the flight before power-down but cannot act on the ignored save result | Shutdown cannot reliably report or retry a failed finalization |
| Other writers | Profiles, routes, waypoints, web handlers, and diagnostics use different save strategies | Fixes confined to flight JSON leave similar weaknesses elsewhere |

These flight-save weaknesses were present in `v0.2.3` and in the code reviewed during the
original investigation. Current `storage/sd_card.*` implements firmware/USB ownership states and
handoffs, with Phase 1 logbook transaction coordination. Extend and audit that design across all
storage users in Phase 2 rather than introducing another ownership mechanism. Its presence does
not establish that USB caused this incident or that every caller already observes it correctly.

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

## Phase 1: implemented focused logbook hardening

The 2026-10-22 scope replaces the original proposal to keep failed completed flights in RAM for
later background retries. Complete one short save sequence, report its outcome, preserve recoverable
files, and release the resources. A previous failure must not prevent recording a new flight.

### 1. Share one checked logbook replacement routine

Use the same narrow routine for placeholders, first-fix/time updates, completed flights, and Leaf Log
upload/rejection status updates. Leave unrelated storage callers for Phase 2.

Required sequence:

1. Require firmware filesystem ownership and serialize the complete logbook transaction against
   recovery, readers, other logbook saves, and deletion. Coordinate the affected ownership transition
   boundaries; an ownership check only at open is insufficient if a handoff can occur mid-save.
   Use the existing ownership design, not a second ownership mechanism.
2. Resolve an earlier incomplete transaction before reusing its temporary or backup filename.
   Do not truncate the only valid candidate or overwrite unresolved conflicting evidence.
3. Build the JSON document, including the revision/completion metadata below, and reject
   allocation/overflow failures. Measure the expected serialized length and check the exact
   written count, including any trailing newline.
4. Write a temporary file in the same directory through buffered output. Check write errors,
   `fflush()`, `fsync()`, and close, preserving the original failure if cleanup also fails. Use one
   handle/buffering model; close a failed handle before a safe retry.
5. Reopen and validate the temporary file: expected length, complete JSON with no unexpected
   trailing content, supported schema, matching flight identity, valid ordering metadata, and
   required fields for the intended placeholder or completed record. Parsing as `{}` is insufficient.
6. Preserve the previous valid main file as `.bak`, then promote the verified `.tmp` to the main name.
   Check each operation and any rollback. Keep at most one ordinary previous-version backup per
   flight; rotate an older backup only after the new temporary and current main are validated.
   A failed rotation or promotion must preserve the surviving valid candidates.
7. Confirm the promoted path is readable and valid before treating the save as successful. Retain
   the previous valid backup. Conflicting evidence is an exception to routine cleanup, not a reason
   to delete the only recoverable copy.

Keep existing filenames, schema identity, and flight fields. Add only compatible optional metadata;
do not rewrite old files just to migrate them. Use stream serialization and one JSON document at a
time across serialization, validation, and recovery. Bound validation size and nesting without
rejecting legitimate existing files or discarding additional fields during upload-status updates.

### 2. Add revision and completion metadata for automatic ordering

Move this small addition from Phase 2 into Phase 1 so users do not have to arbitrate recovery files:

- Add a per-flight revision and explicit completion state to newly written records. Define exact
  field names/types and validation in implementation; compatibility tests must cover all readers.
- Advance the revision for each replacement, including Leaf Log status updates. Account for all
  surviving validated candidates, including an unpromoted temporary, before assigning another
  revision. A failed attempt must not allow different payloads to reuse an ordering value.
- Preserve identity, the original ending time, and completed state across retries and upload-status
  updates. A retry of the same logical save must not become a different flight or a later landing.
- Define equal-revision conflicts, mixed legacy/new candidates, and counter overflow explicitly;
  do not silently wrap the counter or infer ordering from FAT timestamps.
- Recover the newest validated completed revision when ordering is established; never let an
  incomplete placeholder displace a known completed record merely because of its revision.
- Completion means the flight summary is finalized. It does not certify IGC write/close success,
  physical power-loss durability, or upload acceptance.
- Support older files without the metadata. Do not mark them invalid or upload-ineligible solely
  because the fields are absent, and do not invent ordering for conflicting legacy records.
- Newly marked incomplete records must not become upload-eligible merely because JSON now
  contains the early track path. Leaf Log status updates must preserve completion and flight content.

### 3. Replace destructive recovery with automatic candidate validation

- Validate main, `.tmp`, and `.bak` independently. Existence alone is never a validity test.
- Recover a valid replacement when the main file is missing, empty, or malformed. Prefer a
  completed record over an earlier placeholder for the same flight when the evidence is clear;
  use the new metadata where available.
- Preserve conflicting valid completed records when ordering cannot be established, including
  legacy conflicts and inconsistent equal revisions. Record unresolved ambiguity in compact
  internal diagnostics; never ask the user to choose filenames or compare duplicate records.
- Keep one normal logbook entry per flight, with recovery candidates excluded from the ordinary
  list. Keep a valid main as the displayed entry where safe, without claiming an uncertain record
  is the latest. If no safe selection is possible, preserve the candidates and report a simple
  unavailable/save-failure outcome rather than asking for arbitration.
- Clean alternatives only after validation establishes they are redundant. Unresolved recovery
  for an older flight must not block unrelated new flights.
- Discover candidate sets even when only `.tmp` or `.bak` exists. Existing logbook scans enumerate
  one logical path per set; recover the selected set before presenting its summary or classifying
  it for upload, after closing the directory iterator. Leaf Log discovery remains sliced and uses
  its existing SD-backed queue. Resolve a flight's candidates before saving to its path. Do not
  parse every historical main file or scan card sectors on boot.
- Process one candidate set at a time. Do not mutate directory entries under a live iterator in a
  way that skips entries; close/resume safely or use an explicitly bounded work list.
- Serialize recovery against saves and readers. Opening a summary must not remove an active
  writer's temporary file.
- Intentional deletion must remove associated candidates and coordinate with an active save so
  a deleted flight cannot reappear. Cover summary, logbook-menu, and web deletion paths. Check
  each removal, retain the main until candidate/track cleanup succeeds, and permit retry after
  partial failure. Show "Not fully deleted" on the device and a simple inline web deletion error;
  record technical details internally. Routine redundant-temp cleanup errors are diagnostic,
  not a false claim that the user requested deletion or that a verified save failed.
- Handle unsynced-to-timestamp filename changes together with their recovery candidates so old
  paths do not produce duplicate flights or orphan the only valid copy.

This is filename-based recovery, not an on-device FAT checker or unreferenced-sector scan. Keep
the incident image and user data outside the repository; use small synthetic test fixtures.

### 4. Finish each completed flight with bounded retries and release RAM

At flight stop, including a stop initiated by shutdown:

1. Check `finalize()` and freeze the completed-flight data once: statistics, flight ID/path,
   pilot/glider context, track association/status, relevant clock/timezone validity and values,
   and the actual ending time. Reuse existing flight state where practical instead of duplicating it.
2. Attempt the checked save. If it fails, inspect/reconcile the surviving candidates before another
   attempt. Complete a verified temporary's promotion or recognize an already validated result
   where possible instead of blindly rewriting the whole file.
3. Allow at most two additional attempts (three total), with short pauses and an elapsed-time
   budget. Stop early for clearly persistent errors; do not repeatedly remount, format, or retry
   the same failed handle. A missing/full/damaged card is not fixed by an indefinite wait.
4. Aim initially for about 1-2 seconds for the entire final-save sequence, including retry pauses.
   This is a measurement target, not a guaranteed hard deadline: an individual blocking filesystem
   call may overrun the application budget. Check the budget before starting further work, measure
   underlying failure timeouts, and document the observed worst-case completion/shutdown delay.
   Do not try to enforce a deadline by abandoning a live filesystem operation unsafely.
5. Finish with a verified success or an explicit save failure. Preserve useful named candidates,
   close handles, and release temporary documents, buffers, and any completed-flight retry state
   on both outcomes. Do not allocate a session-long pending snapshot, multi-flight queue, retained
   JSON document/string, dedicated retry task/stack, or later background-finalization retry.
   Only compact status/error bookkeeping and the small metadata needed by normal active-flight
   handling may remain. Measure the added steady-state footprint as well as peak save memory.

UI and timing requirements:

- A brief "Saving flight..." dialog and temporary prevention of another flight start are acceptable
  only while the bounded completion sequence runs. No acknowledgement is required to finish it.
  Once terminal success/failure is reached, an older failure must never block a new flight.
- Flight start, placeholder, first-fix, and clock/time updates occur during normal instrument use:
  no saving dialog, deliberate multi-second pause, or immediate multi-attempt retry loop there.
  Make one checked attempt at the existing milestone, return control promptly, and defer any
  further attempt to a later suitable milestone or explicitly rate-limited opportunity. Prevent
  an unsuccessful placeholder flag from producing a retry/write storm on every log update.
- Recovery discovery and validation must respect start/in-flight responsiveness too. Measure
  unavoidable single-operation stalls; do not treat the final-save target as an acceptable added
  delay at flight start. Service required system/watchdog work during retry waits.
- Ordinary shutdown keeps processor and card power available until its bounded save sequence
  finishes. The display may be cleared while saving; a "Saving flight..." indication is permitted.
  Do not release the power latch or hand the card to USB in the middle of the transaction.
  Report failure appropriately before completing shutdown, without demanding user input.
- Low-battery shutdown uses the same nominal time/attempt budget as ordinary shutdown; do not
  shorten the save opportunity just because the low-battery threshold was reached. The maintainer
  reports minutes of operating reserve beyond that threshold, so the bounded, single-digit-second
  save opportunity is acceptable. Neither shutdown path may wait indefinitely for a bad card.
  A stop immediately followed by shutdown must not restart an exhausted retry sequence for the
  same completed flight.
- Report track and logbook outcomes separately. Do not claim "Track saved" from a filename or
  open handle alone, or play/display unconditional save success after logbook failure. Phase 1
  leaves IGC write, synchronization, and close behavior unchanged, so report uncertainty honestly.
- Record the IGC path in the existing first-fix JSON update once the track opens, and preserve it
  in subsequent placeholder/time updates. Do not add frequent checkpoints or change IGC writing.
- Report a compact failure stage/code through existing diagnostics/serial output. Failure reporting
  must not recursively write to the failing card or retain a flight payload in diagnostics.

Releasing RAM after the bounded sequence intentionally gives up delayed retries if storage becomes
usable much later. RAM also cannot survive power loss. Cross-boot recovery relies on surviving named
candidates; neither these files nor summary metadata constitute an in-memory IGC track backup.

### 5. Verify the focused change before release

Add meaningful fault-injection and integration tests covering:

- Short/zero writes, full/missing storage, JSON allocation failure, and sync/close failure.
- Failed backup rotation, promotion, rollback, and interruption between every replacement step.
- Empty/malformed main with a valid temporary or backup; temporary-only/backup-only candidate
  sets; conflicting completed records; idempotent repeated recovery and candidate discovery.
- Revisions across interrupted attempts, equal-revision conflicts, mixed legacy/new records,
  counter overflow, and completion preserved through Leaf Log status updates.
- A failed finalization followed by success, exhaustion, early persistent-error exit, elapsed-budget
  exhaustion, ordinary/low-battery shutdown, and repeated stop/shutdown without renewed retries.
- Frozen landing time/context, cleanup on success and failure, no retained queue or boot-session
  retry allocation, and a new flight starting normally after an earlier terminal failure.
- Intentional deletion from all entry points, concurrent reads/status updates, filename changes,
  and ownership transitions without candidate resurrection or destruction.
- No completion dialog or intentional retry delay at flight start/first fix/time updates; bounded
  recovery and no repeated per-update placeholder write storm.
- Existing files without metadata, legitimate additional fields, valid unsynced/no-GPS flights,
  flights without tracks, and web/export/upload compatibility.
- Early track association without premature upload or a false claim of IGC durability.
- Slow and nearly full cards: peak heap, largest free block, memory released after success/failure,
  steady-state RAM change, save duration, worst instrument stall, and shutdown failure-path latency.

The host-directory simulator verifies application decisions but does not reproduce FAT metadata
ordering. Include a focused FAT-backed test and controlled hardware interruption checks before
declaring power-failure behavior verified. Follow the repository's firmware verification workflow
when implementing the change, and the runtime-report requirements before publishing a release.

Phase 1 exit criteria:

- A failed replacement never intentionally discards the only valid named copy. The completed-flight
  RAM source remains intact throughout the bounded save sequence, then is deliberately released
  on success or terminal failure; it is not retained indefinitely.
- Recovery selects a validated candidate automatically when ordering is established. Conflicts
  preserve evidence and diagnostic visibility without duplicate-file arbitration by users.
- New revision/completion metadata works with existing files/readers and does not change IGC
  behavior or make incomplete records eligible for upload.
- Success requires checked I/O and a validated promoted file. Terminal failure remains visible,
  resources are released, and subsequent flight capture is allowed.
- No new long-lived flight-save buffer/queue or retry task is introduced. Temporary save resources
  are released; any small permanent bookkeeping increase is measured explicitly.
- Added writes occur at existing milestones and bounded failure attempts. No in-flight saving
  dialog, deliberate long start pause, per-fix sync, whole-card scan, or unbounded work is introduced.
- Retry count and elapsed budgets, shared by ordinary and low-battery shutdown, and measured
  memory/stall/shutdown costs are documented. The 1-2 second final-save target is not presented
  as a hardware guarantee.

### Implementation and verification record, 2026-10-23

Implemented in the isolated Phase 1 worktree based on main at `9ae9fe80`. The shared routine is
`src/vario/logbook/logbook_io.*`; its caller contract, recovery triggers, resource policy, and test
instructions are documented in [the logbook README](../../../src/vario/logbook/README.md).

Implementation details fixed by this change:

- New optional fields are `revision` (positive uint32) and `complete` (boolean); schema version
  remains `v0.1.0`. Counter overflow, conflicting equal revisions, and unreadable candidates fail
  conservatively. Supported sparse legacy records and additional fields survive status updates.
- Candidate validation is limited to 64 KiB and nesting 16. Unsupported schemas and over-limit
  candidates remain untouched. JSON fingerprints check readback content without a serialized
  RAM copy; they are transient comparisons, not a persisted checksum or corruption-repair feature.
- A selected recovery candidate receives checked sync/close again before promotion. A previous
  failed sync is not silently converted into success just because parsing later succeeds.
- Finalization uses three attempts maximum, 100 ms between eligible retries, and a 2,000 ms budget
  checked before further attempts. There is no retained queue or background-finalization retry.
  Ordinary and low-battery shutdown share this policy. Filesystem calls or waiting for an already
  active transaction can exceed the application budget; device worst-case timing is unmeasured.
- A terminal save failure is displayed for one second before the normal summary/shutdown flow;
  this feedback interval is separate from the save budget. Opening that failed summary does not
  start another promotion. New-flight capture remains available after the sequence returns.
- Failed ordinary placeholder attempts are rate-limited to 30 seconds. A new first-fix, track,
  or clock milestone may request one earlier attempt. No start/in-flight completion dialog or
  immediate retry loop was added; IGC writer/sync/close behavior is unchanged.
- Directory enumeration discovers one logical path for main/temporary/backup sets. Recovery runs
  after the iterator closes, when the selected summary or queued upload candidate is read. No
  separate boot-wide rewrite or recovery task was introduced.
- Deletion covers recovery candidates, referenced tracks, and matching old filename aliases.
  It keeps the main until cleanup succeeds and rejects deletion of an active flight. Device
  screens show partial failure; an unavailable record still permits navigation/deletion. Web
  errors remain inline. Storage-failure reporting uses serial rather than recursive SD diagnostics.

Completed verification:

| Check | Result |
| --- | --- |
| Repository formatter and whitespace checks | Passed |
| `leaf_3_2_6_release` firmware build | Passed; 103,508 bytes static RAM, 2,383,724 bytes reported flash use |
| `leaf_3_2_8_release` firmware build | Passed from clean implementation commit `2d928d0f`; 103,804 bytes static RAM, 2,366,328 bytes reported flash use |
| Attached Leaf 3.2.8 application flash | Passed; both OTA slots (`0x10000`, `0x340000`) written and verified by esptool. Initial app-mode `COM44` disconnected during connection; the prescribed rescan/retry succeeded on ROM-mode `COM42`. Application USB returned as `COM44`. |
| USB startup follow-up build/flash | Passed from clean commit `2567ddc3`; 103,804 bytes static RAM, 2,366,896 bytes reported flash use; both OTA slots written and verified, with the same app-to-ROM port nudge/retry |
| Native USB diagnostic regression | Passed: real writer and connection lifecycle compiled against a stalled endpoint; 10,000 full-FIFO attempts return without waiting, with stale connection state, partial writes, contention, ISR, detach/reconnect, and failed transitions covered |
| Leaf 3.2.8 normal no-fix saves | Passed on follow-up candidate: three operator-run short flights, responsive starts and normal summaries; each JSON parses, has `complete=true`, revision 2, 11-second duration, and one revision-1 incomplete backup. No `.tmp` files remain; `track.saved=false` and no new IGC are correct without a GPS fix. Operator estimated the stop-only saving display at about 200 ms; this is not an instrumented save-duration measurement. |
| Leaf 3.2.8 absent-card stop and next flight | Passed with Save IGC off: operator removed the card after the placeholder settled, stopped, observed prompt failure feedback/UI return, reinserted, and completed another flight. Card inspection found the first revision-1 incomplete placeholder preserved and the new flight completed at revision 2 with 12-second duration and one prior placeholder backup. No `.tmp` files remain. This tests persistent absence, not all-three-attempt transient recovery. |
| Leaf 3.2.8 staged candidate recovery | Passed: isolated temporary-only (revision 5), backup-only (revision 3), and newer-completed-temporary-over-placeholder (revision 3) fixtures displayed expected durations of 111/222/333 seconds. Post-handoff inspection confirmed correct promoted main payloads/revisions, no remaining `.tmp`, and one prior incomplete backup for the replacement case. No physical power interruption was induced. |
| Leaf 3.2.8 deletion and cold boot | Passed: operator deleted all three synthetic entries through the device and confirmed they stayed absent after unplugged shutdown and a fresh boot. Inspection found no matching main/temporary/backup or old-filename candidates, and both test-only referenced GPX files were removed, including the old alias's track. No `.tmp` files remain; the original five JSON records remain readable and original five IGC files remain present. Save IGC was requested restored to ON. |
| `test-logbook-io` | Passed: injected write/sync/close/rename/cleanup/read/allocation failures, real JSON allocation denial, abrupt process exits, recovery/conflicts/limits, revisions, deletion/aliases, bounded retries, shutdown, new flights, and upload-status preservation |
| Existing Leaf Log sync regression suite | Passed |
| Existing thermal flight-lifecycle/shutdown regression suite | Passed |
| Existing charging dialogs/ownership regression suite | Passed, including 46 native display captures |
| Failure-screen visual review | Save failure, summary/logbook deletion failure, and unavailable-record captures inspected; text and controls do not overlap |
| Disposable FAT32-backed test | Passed on Linux vfat, 512 MiB image, 4 KiB allocation units; same fault/process-exit suite, actual out-of-space failure preserving the previous record, recovery after freeing space, and clean read-only `fsck.fat -n` result |

The final FAT-backed wall-time observation was approximately 3.3 ms for a placeholder and 3.0 ms
for finalization. These are single host observations, not worst-case measurements or ESP32 timing.
Simulator heap telemetry is a stub and is not evidence of target peak heap or memory release.

Target symbol inspection shows `FlightStats` remains 144 bytes and `LogbookEntryFile` grows from
184 to 232 bytes (+48). The new mutex handle and last-error globals occupy another 20 static bytes.
These identified static costs exclude the mutex's runtime backing allocation and active-flight
strings. There is no boot-session completed-flight buffer; actual target heap/stack measurements
remain pending.

Built `leaf_3_2_6_release` application SHA256:
`0323b3eb3702720b805392ed71d7368b1885ec202b52e7924b926d163564c7fb`.

Initial flashed `leaf_3_2_8_release` application SHA256:
`2889ca71122ee5a30c1a4f9ee59a3f49b66c71f61296e34dbe4c035a6ac9875b`.
This binary identifies clean commit `2d928d0fbaa141d521037822b0610f4f67c78a5a`.

USB-startup follow-up flashed application SHA256:
`ef71fc6f0f2890f46fa5765f41ed64cf2926e0f373cce1a425e0c96e316ffe60`.
This binary identifies clean commit `2567ddc35460e244bd7bfcb961a8b4e7165bf365`.

The initial implementation verification found no attached Leaf and did not attempt a flash. A
Leaf 3.2.8 was subsequently attached and flashed as recorded above; manual no-fix saves,
absent-card/new-flight, staged recovery, and deletion/cold-boot checks passed as recorded in the table.
Hardware checks, using
disposable/backed-up SD data, are:

- Repeat normal timer start/stop saves, with and without a usable GPS fix; inspect summaries,
  final JSON, retained backups, and temporary-file cleanup. USB deliberately disconnects when
  leaving charge mode, so an attached USB monitor cannot collect subsequent `logbook finalization`
  lines in normal operation. Use a saved developer runtime capture for instrument/button-task
  stalls and heap observations, or explicit test instrumentation for exact save telemetry.
  Compare repeated-cycle heap observations without treating
  the before/after snapshot as a measurement of peak allocation or post-reset release.
- With Save IGC temporarily off, start a test flight, allow its initial placeholder to settle,
  remove the card, and stop. Check the short failure display and restored UI responsiveness.
  Reinsert, then start/stop a new flight and confirm that the earlier failure does not block it.
  This exercises known-absent media; it does not reproduce an interrupted in-flight IGC write or
  a transient failure requiring all three attempts.
- Stop via ordinary shutdown with a flight active, then restart and inspect its saved summary.
- While the host safely owns the card, stage isolated synthetic temporary-only, backup-only,
  and newer-temporary candidate sets. Release host ownership, select them in the logbook, and
  inspect promotion/cleanup on the next host handoff. Delete a test record, restart, and verify
  that it does not return. Preserve existing flight files and restore changed test settings.

The first physical check exposed a charge-to-on freeze with a USB serial capture open: the LCD
went blank and startup did not finish. Closing the capture and retrying on the same firmware
restored normal startup. The installed Arduino USBCDC writer loops without a deadline while its
FIFO is full; software USB detach can leave cached connection/DTR state asserted with no host
able to drain it. A follow-up replaces only Leaf's native USB diagnostic writer with bounded,
best-effort FIFO writes and a non-waiting transmit lock, and disables output before software
detach. Reconnection enables it again. Saturated, contended, disconnected, and ISR diagnostics
may be dropped; no extra output queue is allocated. A focused host test compiles the real writer
against a permanently stalled endpoint, including stale connected state, partial writes, lock
contention, detach, and reconnect. On the flashed follow-up candidate, the operator confirmed
that charge-to-on now reaches normal operation with the USB serial monitor open. The monitor
captured the center press/hold and observed the expected USB detach. This confirms one startup
cycle under the previously failing condition; repeated monitored startup cycles and GPS-fix flight
checks remain pending.
The follow-up also passed the logbook regression and charging suite with 46 display captures.

GPS/IGC association, active-flight shutdown, peak heap/largest block/stack headroom,
slow/nearly-full-card timing, and controlled power interruption across multiple cards still need
qualification. Linux vfat and process-exit tests do not reproduce target FatFs metadata ordering,
SD-controller power loss, or the original incident's root cause. Reviewed exact-candidate runtime
reports remain required before release publication; this implementation is not a release approval.

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
Recheck the boundary before USB presentation and power-latch release. Both ordinary and low-battery
shutdown use bounded save/drain timing so retrying a bad card cannot indefinitely delay power-off;
Phase 1 does not shorten the retry budget at the low-battery threshold.

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

Revision/completion metadata for logbook ordering is now part of Phase 1. Phase 2 extends and
audits that policy across other durable file families where needed, preserving legacy compatibility
and completed-flight state through later updates. Do not use coarse FAT timestamps as transaction
sequence numbers.

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
| Bounded finalization | Reuse flight state only during the final-save sequence; release retry resources afterward | At most three total attempts within an elapsed budget; no session-long retry queue |
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
- Long-lived RAM snapshots, multi-flight pending queues, and delayed background finalization
  retries are outside the agreed Phase 1 plan. Reconsider only with evidence of useful recovery
  opportunities and an explicit memory/lifecycle budget.
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
