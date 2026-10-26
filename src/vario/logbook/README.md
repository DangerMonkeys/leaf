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
flight save and its immediate RAM summary perform **zero JSON reads**. There is no readback hash:
comparing original float values against their parsed JSON round trip caused false failures in
0.2.4-968. One previous `.bak` is retained. Leaf Log status updates read the main once, modify it,
and use the same checked replacement routine.

Quiet recovery runs when opening the device logbook/web app and during Leaf Log's eligibility
scan. It requires a stopped flight timer and firmware filesystem ownership. Healthy mains are
read without inspecting their backups. A temporary, missing/malformed main or incomplete record
triggers one candidate-set recovery attempt per viewing/upload session (up to 16 affected sets).
Only recovery compares parsed candidates: completed records outrank placeholders; unordered
completed candidates and unreadable/unsupported candidates are preserved. Unresolved conflicts
remain viewable through a readable main but are excluded from upload/status writes.

Directory discovery is sliced (up to 16 entries/10 ms for viewing repair), closes its iterator
before mutations, and can stop between records. A new flight or SD ownership handoff closes it.
Leaf Log reuses its bounded scan and existing SD-backed queue. No boot scan, new storage task,
repair journal or flight-time retry loop is added. A single filesystem call can exceed a slice.
Charging-mode recovery must tolerate immediate power loss on unplug: it does not rewrite candidate
data or delete the chosen source before promotion. This is application-level ordering, not a
guarantee against interrupted FAT metadata or SD-controller failure.

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
Unknown temporary files are preserved for idle repair; a writer can replace only its own failed
staging file. No IGC synchronization or continuous-write behavior changes. Save failure never
prevents a subsequent flight from starting after finalization returns.

Deletion removes candidates and associated tracks before the main, checks each result, and also
finds old filename aliases by the generated flight-ID suffix. It never runs recovery as a deletion
precondition. A flight currently being recorded cannot be deleted. After partial deletion the UI
quietly refreshes the available complete, readable records and their counts/navigation. It does
not repair immediately after deleting or claim success when deletion failed. Save failure is the
only per-flight exception message; cleanup/repair failures remain serial diagnostics. Whole-card
unavailability is distinct from an empty logbook. Failed redundant-temp cleanup is diagnostic
rather than a false save failure. Successful repair removes redundant `.tmp`; the `.bak` remains
intentional, and ambiguous or sole useful candidates may remain until another recovery opportunity.

Standalone saving, opening, empty/unavailable and shutdown-failure messages use a centered 96×96
rounded frame with centered text. Shutdown details say "No IGC file" or "IGC unverified". The
inline "Logbook save failed" warning on the immediate flight summary keeps its existing layout.

## Storage tests

In the normal GCC simulator container, run `make -C sim -j8 test-logbook-io`. It exercises the real
routine with injected failures, abrupt child-process exits at transaction boundaries, and callers.
Set `LEAF_TEST_SCREENSHOTS=sim/build-tests/sd-hardening/screenshots` when running the test binary
to export save/deletion failure screens.
Set `LEAF_FIELD_EVIDENCE` to a read-only folder containing device subfolders with `logbook`
directories to additionally recover disposable copies and check that completed temporary payloads
are promoted byte-for-byte. Never run the tests against original evidence as the writable card root.
`node sim/tests/logbook_web.js` checks the shipped web JavaScript and its repair polling,
quiet deletion refresh, and distinction between unavailable storage and no flights.

For a disposable FAT32-backed run, build `sim/Dockerfile.fat-test` as `leaf-sd-fat-test`, then run
`sh sim/scripts/test-logbook-fat.sh` in that image with the repository mounted at `/leaf`, working
directory `/leaf`, and elevated container mount/loop access. Build the simulator test first. The
script creates/formats only its temporary 512 MiB image, exercises an actual full-volume failure,
unmounts it, checks it with `fsck.fat -n`, and removes the image. This tests Linux vfat behavior;
it does not reproduce ESP32 FatFs/SD-controller behavior or qualify physical power-loss durability.

## Bus log format

The bus log is mostly human-readable as plain text. Each line contains a message logged from the message bus. The first character indicates the type of message and then the format of the data on the line depends on that message type. The message types and formats can be found in the `on_receive` methods in [bus.cpp](./bus.cpp).
