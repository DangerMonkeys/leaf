---
title: Leaf Log Auto-upload Backlog
description: Implemented charging-mode upload, device pairing, USB handoff, and remaining qualification.
---

# Leaf Log Auto-upload Backlog

Status reconciled **2026-10-24** against `main` at `18fc69af`: auto-upload, device-level NVS
pairing, logbook result/status handling and USB ownership are implemented. Preparation/queue
improvements merged in `2b9cb5e1` (#418), the linked-device website shortcut in `6c51405c`
(#422), and charging dialogs/live input in `18fc69af` (#425).

The original proposal is superseded by the current behavior below. In particular, the uploader
does not promise an unlimited batch, automatic USB handoff on every transient failure, sleeping
during retry backoff, or cancellation from any button.

## Eligibility and scope

- Uploads run in USB charging mode (`PowerState::OffUSB`) with `Settings > Leaf Labs > Leaf Log`
  enabled, a linked device token and firmware-owned mounted SD storage.
- Leaf connects only to saved WiFi networks; it never starts the access point for auto-upload.
  Missing/unavailable networks enter the same retry path as other transient failures.
- Factory discovery/commissioning can temporarily reserve charging work. If no factory network
  is found, ordinary Leaf Log/USB behavior resumes.
- Entries are discovered through `/logbook/*.json`. Orphan IGC files under `/tracks` are not
  migrated, and KML/no-saved-track entries are not uploaded.
- All eligible entries use the account owning the device token, regardless of local pilot IDs.
- The power-on ownership flow is also implemented; see
  [USB ownership](usb-mass-storage-power-on-ownership.md).

## Charging workflow and progress

`LeafLogSync` owns the state machine. The charging display/timer cadence is 500 ms; input is
polled independently and eligibility scans advance between display ticks.

1. Reserve firmware storage and enumerate logbook paths into `/.leaf-log-scan.tmp`.
2. Close the directory iterator before classification or result writes can rename entries.
   Process at most 32 scan steps or approximately 10 ms per slice, checking cancellation
   between operations. A single filesystem operation can exceed that time budget.
3. Classify entries and persist terminal local rejections. Save up to **100 pending flights**
   into `/.leaf-log-pending.tmp`; the displayed session total covers that queue.
4. Connect WiFi and obtain network time when needed. Stream one queued IGC at a time in a
   FreeRTOS worker while the main loop continues input/display work.
5. The main loop consumes worker results, records delivery/rejection, updates public account
   metadata after delivery, and advances the existing queue. It does not rescan the whole
   logbook after every flight.
6. On completion or confirmed cancellation, close scratch/track handles and disconnect WiFi
   before USB presentation. Delete scratch queues only while firmware owns the filesystem.
   Flights added after the scan or beyond the cap wait for a later session.
7. An explicit host eject can start another session. After that session, leave the medium
   ejected rather than re-presenting it in the same connection.

The scratch queues are rebuildable indexes. Persisted logbook results remain authoritative;
a retry keeps the current flight/queue position rather than silently dropping it.

### Charging dialogs and controls

- Animated panels show scanning, WiFi, network connection, transfer, completion and retry pause.
  Transfer/completion show the compact current/total counter when known.
- A short center press shows `Cancel Uploads?`. A second within five seconds confirms USB
  handoff; expiry dismisses the confirmation without cancelling. Transfer continues while
  confirmation is visible, and completion supersedes it.
- Directional presses do not cancel. Holding center cancels transfer work and requests power-on
  directly, without first presenting the SD card.
- The upload worker finishes/aborts and closes its file before USB handoff or power-on.
  The UI can show `Opening USB Drive`, `Closing USB Drive` or `Turning On`.
- The final check or X is shown for five seconds after handoff, then a compact result remains
  throughout the charging session. Successful batches (including the 100-flight cap) and an
  empty eligible queue show a check. Newly encountered rejections, unreadable records,
  missing storage/account linkage, authorization failure or cancellation with unfinished work
  show X. A transient failure followed by successful retries shows a check; retry waits retain
  their large paused animation. Previously recorded rejections do not fail every later session.
- The compact panel shows USB only, Leaf Log only, or both with a two-pixel vertical divider.
  USB requires both presented storage and a configured USB host; power-only chargers and
  charge-only cables do not show USB availability. Host configuration does not prove that the
  computer mounted the filesystem. Leaf Log results remain visible independently of USB
  handoff failure, host connection or eject. Rejections encountered during this charging
  session remain problems across eject-triggered rescans; a new charging session resets results.
- With Leaf Log disabled, a failed USB handoff shows `USB Drive Not Ready` for 30 seconds.
  With a final Leaf Log result, that result remains visible instead. A center click retries the
  handoff without restarting uploads or changing their result, and holding center can still
  request power-on.
- The footer shows the combined firmware/hardware version, or `NO SD CARD!` when absent.

## SD ownership

The card is never intentionally presented for raw MSC I/O while firmware is using FAT.
`SDCard` coordinates `FirmwareReserved`, `FirmwareUploading`, transitions and `HostOwned`.
Presentation closes firmware filesystem access; explicit eject/disconnect allows reacquisition.
Suspend, inactivity and stop/spindown without `load_eject` do not release host ownership.

Normal operation does not expose the SD LUN. A deliberate center hold can reclaim a presented
medium after draining MSC callbacks; it cannot flush unsent host caches. Diagnostics pause while
USB owns the card. Broader storage transaction/recovery guarantees remain in
[SD card write robustness](sd-card-write-robustness.md).

## API and result records

The source contract is `leaf-log/docs/device-api-contract.md` in the adjacent Leaf Log repository.
The firmware sends:

```text
POST https://log.leafvario.com/api/ingest
Authorization: Bearer llk_...
X-Filename: <original-name>.igc
Content-Type: application/octet-stream

<raw IGC bytes>
```

The limit is 5 MiB. A successful response includes `flightId`, `status`, `deduped` and
`account: { handle, displayName }`. Exact bytes are deduplicated per account. HTTP 200 with a
nonempty `flightId` records delivery, including `deduped: true` or server `status: "failed"`.
Invalid JSON or a missing ID is transient. No pilot ID is sent for routing.

A parseable logbook entry gets one mutually exclusive result:

```json
"leaf_log": { "flight_id": "tn8t" }
```

or:

```json
"leaf_log": { "rejected": "too_large" }
```

Supported rejection reasons are `invalid_logbook`, `unsafe_track_path`, `missing_track`,
`empty_track`, `too_large` and `invalid_igc`. A pending entry must describe a saved IGC with
a normalized path under `/tracks`, an existing nonempty file within the size limit, and no
delivered/rejected marker. A valid legacy IGC path can establish its format.

Schema-invalid but parseable entries receive a terminal reason where possible. Syntactically
broken JSON without a valid recovery candidate stays untouched and gets no sidecar. KML and `track.saved == false` are
`NotApplicable`. Rejections suppress automatic retry; repair tooling/manual edits may clear one.

Since `605810eb` (#430), `LogbookStore` uses the shared checked logbook save/recovery routine
for result patches. It preserves unknown fields and completion state, advances the revision,
checks write/sync/close and readback, and retains a previous backup. Upload classification also
resolves recoverable candidates. This completes the focused Phase 1 result-update work, not
physical power-loss qualification or the firmware-wide Phase 2 audit; see the
[SD robustness backlog](sd-card-write-robustness.md). The optional result is documented in
`src/vario/logbook/logbook.yaml`.

Both logbook UIs use shared summary parsing for `NotApplicable`, `NotUploaded`, `Uploaded`
and `Rejected`. With Leaf Log enabled, the device shows leaf glyphs 141/140/143; the web app
shows accessible icons and rejection labels. No icon is shown for `NotApplicable`.

## Failure, retry and timeout behavior

- HTTP 401 stops the batch, clears the invalid token and persists reconnect-required state.
- HTTP 400 records `invalid_igc`; HTTP 413 records `too_large`; later queued flights continue.
- Network/server/response failures leave the entry pending and retry after 5, 15 and then
  60 minutes. Success resets the retry progression.
- **Current transient failures retain firmware ownership even when a computer is connected.**
  They show the paused panel until retry or user action. The old proposal's automatic fail-open
  handoff is not implemented. Confirm cancellation to get USB access, or hold center to power on.
- Backoff currently keeps the charging loop awake (`canSleepWhileCharging()` is false).
  Sleeping between retries remains an optimization to assess, not an implemented property.
- The client configures a 10-second HTTP connection timeout and 15-second socket timeout.
  Network-time wait is eight seconds. A 60-second deadline is checked by the upload stream;
  it is not a proven end-to-end wall-clock bound across DNS, blocking TLS/socket operations
  and response-body retrieval. Cancellation can wait for an in-progress blocking operation.
  Measure real network failure/cancellation latencies before claiming tighter bounds.

## Device pairing and account identity

One credential is stored per physical device in NVS namespace `leafLog`, outside the SD profiles.
The credential store stages token/handle/display name in alternate slots, verifies readback,
then commits the active-slot selector. Old single-slot NVS records are readable; experimental
profile-file tokens are not migrated. Re-pair those devices.

- Pairing does not require selecting a local pilot. Profile validation/UI removes the obsolete
  `leaf_log` profile object; logbook pilot snapshots remain unchanged.
- Public account handle/display name are cosmetic snapshots, never routing keys. No account
  email is returned/stored by this flow. Successful uploads refresh the cached public label.
- Disabling Leaf Log retains the token; re-enabling resumes without pairing again.
  Firmware settings reset and factory NVS reset clear pairing; replacing the SD card does not.
- Re-pairing stores the new token before best-effort self-revocation of the old one through
  `POST /api/devices/revoke-self`. Cleanup failure does not roll back the new pairing.
- Unlink requires confirmation, attempts self-revocation, then clears local pairing even if
  server cleanup fails. Tokens lost through reset/corruption remain manually revocable online.
- The web card shows link/reconnect state and pairing/unlink controls. A linked device also
  offers the Leaf Log website shortcut (#422). It does not add a sync dashboard or SD scan.

The adjacent Leaf Log implementation includes public account fields on pairing/ingest,
self-revocation, token creation/last-use local times and latest-flight context.
`DeviceToken.lastFlightId` and `lastUsedAt` update on successful ingest, including deduplication;
missing/deleted historical associations are nullable. These are implemented source features,
not evidence that a deployed server was tested by this documentation review. A stable hardware
identity guaranteeing one live token after credential loss remains outside this design.

## Verification and remaining work

Existing `test-leaf-log-sync` covers classification/queue reuse, the 100-flight cap and later
sessions, transient retry, unauthorized responses, cancellation and ownership outcomes.
`test-charging-dialogs` covers native artwork, animation phases, live center input/confirmation,
completion and handoff states; recorded output is in
[the #425 verification record](../../ui-reviews/charging-dialog-revamp/verification.txt).

Retain these as qualification work rather than claiming the entire original test matrix passed:

- Real saved-network/TLS/NTP success and failure, slow/stalled responses, revoked tokens,
  re-pair/unlink cleanup failure and observed cancellation deadlines.
- Repeated physical USB mount/eject/suspend/disconnect, center-hold reclaim and active host I/O,
  including power-only charging, missing/removable SD cards and factory discovery interactions.
- Physical power-interruption and result-update tests on the integrated candidate, coordinated
  with the SD robustness backlog. Phase 1 simulator tests already cover injected short writes,
  recovery and upload-status preservation; device staged recovery has also passed.
- Long charging sessions with many flights, slow/full cards and retry pauses: measure input
  responsiveness, worker stack/heap use, power cost and watchdog behavior.
- Consider allowing sleep during retry backoff while preserving live input and retry deadlines.
  Any change to automatic USB handoff on transient failure needs an explicit behavior decision.

Orphan IGC migration, cross-account flight transfer/sharing and a permanent hardware-token
identity model remain outside the firmware auto-upload scope.
