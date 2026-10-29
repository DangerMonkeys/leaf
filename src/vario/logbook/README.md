# Leaf logbook

[View the per-flight logbook entry schema with the ReDoc viewer](https://redocly.github.io/redoc/?url=https://raw.githubusercontent.com/DangerMonkeys/leaf/refs/heads/main/src/vario/logbook/logbook.yaml)

Each saved flight should have one authoritative JSON file in `/logbook`. Any webserver list or index response should be derived from those files, so the index can be rebuilt if entries are added, edited, or deleted.

## Checked saves and recovery

`logbook_io` is the shared replacement routine for flight records and Leaf Log status updates.
Hold its transaction `Guard` across read/modify/commit operations. The active flight owns its
revision and small `PendingWrite` state; it does not inspect files to prepare a save. No JSON
document or completed-flight retry queue is retained between calls.

New records add optional `revision` (positive uint32) and `complete` (boolean) fields without
changing the schema version. Completion describes the summary, not IGC durability. Status updates
preserve flight content, advance the revision, and retain completion. Legacy files remain readable;
sparse legacy status updates preserve their existing fields. Unknown schemas and records beyond
the 64 KiB/16-level validation limits are preserved and reported, not treated as disposable corruption.

Writes use a same-directory `.tmp`, exact byte counts, checked stream errors, `fflush`, and checked
`fclose`, followed by checked backup rotation and promotion. ESP-IDF's FatFs `f_close` already
synchronizes the file; no additional explicit `fsync` is requested immediately before it. A normal
placeholder save and its immediate RAM summary perform **zero JSON reads**. Completed saves
also perform one sequential byte readback using a 256-byte stack buffer. The writer records
the exact serialized byte count and FNV-1a-64 checksum (including the newline), then compares
the promoted main against those bytes before deleting `.bak`. It never compares original
floats against parsed JSON round trips, which caused false failures in 0.2.4-968. Verification
failure preserves the backup and uses the existing bounded stop-time retry budget; a retry
checks the same promoted file without rewriting or rotating its backup. Backup deletion failure
alone is diagnostic: the verified flight remains saved. Leaf Log result updates use the same
verified cleanup for completed records. Placeholders retain recovery candidates until completion.

Recovery runs only during charging-page Leaf Log upload preparation or the explicit
**Logging > Repair Logs** action. Summary reads, ordinary device/web browsing, deletion refresh,
and downloads never initiate recovery. Manual repair is unavailable with the flight timer running;
a new flight, cancellation, or storage ownership loss stops maintenance safely between operations.
A main with a backup is now a maintenance candidate even when readable. Maintenance validates
the candidate set, reads back a promoted candidate, and deletes a backup only when the completed
main clearly supersedes it. Completed records outrank placeholders; ambiguous, unreadable,
unsupported or malformed backups are preserved and reported as unresolved. Automatic upload
scanning cleans every enumerated record, while retaining the existing session upload limit.

Nominal browsing scans only main `.json` filenames, excluding the active flight. Incomplete or
unreadable mains participate in counts. Opening and paging both show centered `Loading / Flights`.
Selecting a confirmed malformed/incomplete main shows `Repair needed / Run / "Repair Logs"`;
a busy lock or temporary read failure shows `Could not read / flight / Try again`. Neither read
path starts maintenance or treats a transient failure as proof of corruption.
Auxiliary-only records remain hidden until explicit maintenance or upload preparation recovers
them. Opening/paging uses one incremental directory pass and one selected JSON read, with a
fixed number of working filenames, not a per-flight RAM index. Device scans run between display
frames; web requests poll HTTP 202 until their read-only scan is ready.

Manual checking and directory scans yield after at most 16 entries or a 5 ms soft work slice.
Checking queues suspect paths into `/.logbook-repair.tmp` on SD. It closes the logbook directory
before processing that list, avoiding invalidated FAT iterators during renames. Each queued path
gets one recovery attempt, without the former 16-repair ceiling or a RAM filename-history array.
The work list is disposable and rebuilt on each request; its size grows only with suspect records,
not RAM use. Cancellation/completion removes it when firmware owns the filesystem. Individual
filesystem calls and a candidate-set repair can exceed a work slice.

The action shows rounded `Checking logs`, then `Repairing logs` if needed, followed by `No problems
found`, `Repair complete`, `Some logs not repaired`, or an explicit failure/cancellation result.
Each phase is visible for at least one elapsed second; button-triggered redraws cannot shorten it.
Results return to Logging settings. Back/center cancels between operations. Leaf Log keeps its
existing charging UI and SD-backed upload queue; no recovery is introduced into flight operation.
Checked writes and bounded final-save retries remain in place. None of these
application-level rules guarantee durability across interrupted FAT metadata or SD-controller failure.

Finalization freezes the ending time/context and makes at most three attempts, pausing 100 ms
between eligible retries, with a 2,000 ms budget checked before further attempts. Persistent errors
stop early. Individual filesystem calls and waiting for an already-active transaction can overrun
that application budget. Ordinary and low-battery shutdown use the same policy. A terminal failure
is shown inline on the immediate summary, or for one second on shutdown without acknowledgement.
The shutdown notice is separate from the save budget. The summary uses the frozen in-memory
flight data and does not trigger repair. The physical speaker is muted before final IGC/JSON work
and its prior mute state is restored before the existing result sound.

Start/first-fix/clock saves do not show the completion dialog or enter the immediate retry loop.
Clock/first-fix/track milestones are coalesced into the initial placeholder where available, with
at most one later milestone write. Failed placeholders do not trigger periodic retries. Final-save
retries reuse a checked staging file after promotion failure instead of rewriting or rereading it.
Unknown temporary files are preserved for explicit maintenance or charging uploads; a writer can replace only its own failed
staging file. No IGC synchronization or continuous-write behavior changes. Save failure never
prevents a subsequent flight from starting after finalization returns.

Deletion removes candidates and associated tracks before the main, checks each result, and also
finds old filename aliases by the generated flight-ID suffix. It never runs recovery as a deletion
precondition. A flight currently being recorded cannot be deleted. After partial deletion the UI
quietly refreshes main-file records and their counts/navigation. It does
not repair immediately after deleting or claim success when deletion failed. Save failure is the
only immediate save exception message; manual maintenance reports unresolved repair results. Whole-card
unavailability is distinct from an empty logbook. Failed redundant-temp cleanup is diagnostic
rather than a false save failure. Successful maintenance removes redundant `.tmp` and provably
superseded `.bak` files; ambiguous or sole useful candidates remain for another recovery opportunity.

New records without a trustworthy GPS-derived calendar date use
`date_not_available_<seconds-since-boot>_<flight-id>.json`. They are primary records, never disposable
errors. During maintenance, resolved legacy `unsynced_` names are renamed to the new prefix without
changing JSON payloads or track paths. Destination collisions preserve both names and are reported.
Upload queues follow the returned filename. Both prefixes remain readable and sort together below
dated flights using filenames only. Existing clock metadata retains its schema's `unsynced` value.

Standalone saving, loading, empty/unavailable and shutdown-failure messages use a 96×96
rounded frame 3 pixels above the LCD center, with text centered inside. This leaves space above
the logbook Delete row. Shutdown details say "No IGC file" or "IGC unverified". The
inline "Logbook save failed" warning on the immediate flight summary keeps its existing layout.

## Storage tests

In the normal GCC simulator container, run `make -C sim -j8 test-logbook-io`. It exercises the real
routine with injected failures, abrupt child-process exits at transaction boundaries, and callers.
Set `LEAF_TEST_SCREENSHOTS=sim/build-tests/sd-hardening/screenshots` when running the test binary
to export save/deletion failure screens.
Set `LEAF_FIELD_EVIDENCE` to a read-only folder containing device subfolders with `logbook`
directories to additionally recover disposable copies and check that completed temporary payloads
are promoted byte-for-byte. Never run the tests against original evidence as the writable card root.
`make -C sim -j8 test-logbook-maintenance` exercises 400-entry read-only browsing, repair of more
than 16 records, preserved conflicts, cancellation/storage loss, and minimum dialog visibility;
it captures only changed/new logbook maintenance screens under `sim/build/logbook-maintenance-screens`.
`node sim/tests/logbook_web.js` checks the shipped web JavaScript and its read-only scan polling,
quiet deletion refresh, and distinction between unavailable storage and no flights.

For a disposable FAT32-backed run, build `sim/Dockerfile.fat-test` as `leaf-sd-fat-test`, then run
`sh sim/scripts/test-logbook-fat.sh` in that image with the repository mounted at `/leaf`, working
directory `/leaf`, and elevated container mount/loop access. Build the simulator test first. The
script creates/formats only its temporary 512 MiB image, exercises an actual full-volume failure,
unmounts it, checks it with `fsck.fat -n`, and removes the image. This tests Linux vfat behavior;
it does not reproduce ESP32 FatFs/SD-controller behavior or qualify physical power-loss durability.

## Bus log format

The bus log is mostly human-readable as plain text. Each line contains a message logged from the message bus. The first character indicates the type of message and then the format of the data on the line depends on that message type. The message types and formats can be found in the `on_receive` methods in [bus.cpp](./bus.cpp).
