# Leaf logbook

[View the per-flight logbook entry schema with the ReDoc viewer](https://redocly.github.io/redoc/?url=https://raw.githubusercontent.com/DangerMonkeys/leaf/refs/heads/main/src/vario/logbook/logbook.yaml)

Each saved flight should have one authoritative JSON file in `/logbook`. Any webserver list or index response should be derived from those files, so the index can be rebuilt if entries are added, edited, or deleted.

## Checked saves and recovery

`logbook_io` is the shared replacement routine for flight records and Leaf Log status updates.
Hold its transaction `Guard` across `prepare`, document construction, and `commit`. Preparation
resolves earlier candidates and returns the next revision; commit consumes/reuses the document
for readback. No document or completed-flight queue is retained between calls.

New records add optional `revision` (positive uint32) and `complete` (boolean) fields without
changing the schema version. Completion describes the summary, not IGC durability. Status updates
preserve flight content, advance the revision, and retain completion. Legacy files remain readable;
sparse legacy status updates preserve their existing fields. Unknown schemas and records beyond
the 64 KiB/16-level validation limits are preserved and reported, not treated as disposable corruption.

Writes use a same-directory `.tmp`, exact byte counts, checked flush/sync/close, and validated
readback before promotion. One previous `.bak` is retained. Candidates are validated independently;
newer completed records outrank placeholders. Unordered completed candidates are preserved with a
serial diagnostic. Read-only I/O failures and allocation failures do not establish invalidity.
Recovery synchronizes a candidate again before promotion in case its original write failed sync.

Recovery triggers are a retry/preparation for that flight, reading its summary, or upload
classification. Directory scans discover temporary-only and backup-only sets as one logical path;
they do not rename/delete files under the live iterator. The selected path is recovered after the
iterator closes. Leaf Log discovery uses its existing bounded scan slices and SD-backed queue.
There is no whole-card boot scan or persistent background recovery task.

Finalization freezes the ending time/context and makes at most three attempts, pausing 100 ms
between eligible retries, with a 2,000 ms budget checked before further attempts. Persistent errors
stop early. Individual filesystem calls and waiting for an already-active transaction can overrun
that application budget. Ordinary and low-battery shutdown use the same policy. A terminal failure
is shown for one second before the normal summary/shutdown flow, without acknowledgement; this
display interval is separate from the save budget. Opening a failed summary does not retry again.

Start/first-fix/clock saves do not show the completion dialog or enter the immediate retry loop.
Failed placeholders wait 30 seconds before another ordinary attempt; a new first-fix/clock/track
milestone may request one attempt earlier. No IGC synchronization or continuous-write behavior is
changed. Save failure never prevents a subsequent flight from starting after finalization returns.

Deletion removes candidates and associated tracks before the main, checks each result, and also
finds old filename aliases by the generated flight-ID suffix. It never runs recovery as a deletion
precondition. A flight currently being recorded cannot be deleted. Partial failure is shown as
`Not fully deleted` on the device and `Flight could not be fully deleted` on the web; users can
retry or leave. Failed redundant-temp cleanup is diagnostic rather than a false save failure.

## Storage tests

In the normal GCC simulator container, run `make -C sim -j8 test-logbook-io`. It exercises the real
routine with injected failures, abrupt child-process exits at transaction boundaries, and callers.
Set `LEAF_TEST_SCREENSHOTS=sim/build-tests/sd-hardening/screenshots` when running the test binary
to export save/deletion failure screens.

For a disposable FAT32-backed run, build `sim/Dockerfile.fat-test` as `leaf-sd-fat-test`, then run
`sh sim/scripts/test-logbook-fat.sh` in that image with the repository mounted at `/leaf`, working
directory `/leaf`, and elevated container mount/loop access. Build the simulator test first. The
script creates/formats only its temporary 512 MiB image, exercises an actual full-volume failure,
unmounts it, checks it with `fsck.fat -n`, and removes the image. This tests Linux vfat behavior;
it does not reproduce ESP32 FatFs/SD-controller behavior or qualify physical power-loss durability.

## Bus log format

The bus log is mostly human-readable as plain text. Each line contains a message logged from the message bus. The first character indicates the type of message and then the format of the data on the line depends on that message type. The message types and formats can be found in the `on_receive` methods in [bus.cpp](./bus.cpp).
