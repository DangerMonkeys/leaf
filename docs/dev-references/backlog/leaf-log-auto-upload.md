---
title: Leaf Log Auto-upload Backlog
description: Proposed design for safely uploading recorded IGC flights to Leaf Log while Leaf is in USB charging mode.
---

# Leaf Log Auto-upload Backlog

Status: proposed design. No uploader or logbook migration has been implemented yet.

## Goal

When Leaf is in USB charging mode, automatically upload recorded IGC flights that have not already
been delivered to Leaf Log. Auto-upload must not compete with a computer using the SD card through
USB mass storage.

Auto-upload is enabled only when:

- `Settings > Leaf Labs > Leaf Log` is enabled (`settings.labs_leafLog`).
- The device has completed Leaf Log pairing and has a device bearer token.
- A saved Wi-Fi network is available.
- The SD card is mounted.
- Firmware still owns the mass-storage volume, or the USB host has explicitly ejected it.

Orphan IGC migration is intentionally out of scope. The uploader scans logbook JSON entries and does
not search `/tracks` for unreferenced files.

Changing what happens when the user powers Leaf on while a computer owns the mass-storage volume is
also out of scope. Preserve the current power-on behavior in this release; the ownership and UX
tradeoffs are tracked separately in
[`usb-mass-storage-power-on-ownership.md`](usb-mass-storage-power-on-ownership.md).

## Leaf Log API contract

The source of truth is `leaf-log/docs/device-api-contract.md` in the Leaf Log repository.

Upload one flight with:

```text
POST https://leaflog.norcalflight.com/api/ingest
Authorization: Bearer llk_...
X-Filename: <original-name>.igc
Content-Type: application/octet-stream

<raw IGC bytes>
```

The body is a single raw IGC file, not multipart, with a maximum size of 5 MB. A successful response
has this shape:

```json
{ "flightId": "tn8t", "status": "ready", "deduped": false }
```

Leaf Log deduplicates exact IGC bytes per account. Retrying an uncertain delivery is safe and returns
the existing `flightId` with `deduped: true`.

## Current firmware behavior

- USB power boots Leaf into `PowerState::OffUSB` when the center button was not used.
- `TaskManager::updateWhileCharging()` runs charging work every 500 ms.
- `leaf_usb` tracks USB data-host start, stop, suspend, and resume events separately from USB power.
- It already provides a three-second host-enumeration grace period.
- The SD card is mounted through `SD_MMC` while the same card is also exposed as writable USB MSC.
- MSC callbacks currently call `SD_MMC.readRAW()` and `SD_MMC.writeRAW()` without coordinating with
  filesystem access from firmware.
- Pairing currently stores `leaf_log.pilot_id`, `account_email`, and `token` in
  `/profiles/profiles.json`, although the token itself is already device-level.
- Saved Wi-Fi credentials can already be tried through `leaf_wifi::attemptSavedNetworkConnection()`.

An inactivity timer is not a sufficient safety boundary. A connected host may have a mounted FAT
filesystem and cached writes even when no recent MSC callback has occurred.

## Recommended first version

Prefer Leaf Log for users who have explicitly enabled and paired it. On a new USB charging session,
firmware initially retains the SD card and does not present the mass-storage medium while eligible
pending flights are uploaded. A USB data host may enumerate Leaf during this period, but its MSC LUN
reports that no medium is present.

Show a charging-page status as soon as an upload attempt starts. Example copy:

```text
Leaf Log

Uploading 2 of 5
Press button for USB drive
```

While scanning it should show:

```text
Checking Leaf Log...
Press button for USB drive
```

During network setup it may instead show `Connecting...`. Any short button press cancels auto-upload
and gives the SD card to USB mass storage. Cancellation must remain available during the logbook scan,
Wi-Fi connection, network time synchronization, and every upload.

Mass storage remains effectively unchanged for users who have not opted into Leaf Log:

- Leaf Log setting disabled: present the medium immediately.
- Leaf Log enabled but not paired: present the medium immediately.
- Paired with no eligible pending flights: present the medium after the quick eligibility scan.
- Eligible pending flights: hold the medium while uploading, unless the user cancels.

When uploads finish, are cancelled, or encounter a bounded failure, release firmware access and
present the medium to the host. Network or Leaf Log failure must fail open to mass storage; a failed
sync must never leave the USB drive unavailable indefinitely.

For purposes of the charging-mode uploader, once the medium has been presented, the host owns the
filesystem until explicit eject or disconnect. Neither MSC inactivity nor USB suspend releases
ownership. The Arduino USB framework supports `USBMSC::onStartStop()`; an explicit host eject can
safely return ownership to firmware and resume a cancelled or incomplete upload batch while Leaf
remains plugged in. A user-requested transition to normal powered-on operation retains its existing
behavior in this release and is covered by the separate power-on ownership backlog.

### Charging state machine

Implement a non-blocking `LeafLogSync` or `LeafLogUploader` state machine called from
`TaskManager::updateWhileCharging()`:

1. `CheckingEligibility` - require the Leaf Labs setting, mounted SD card, device pairing token,
   saved Wi-Fi, and at least one eligible pending flight.
2. `PresentingMassStorage` - immediately present the medium when sync is disabled, unpaired, already
   complete, cancelled, or unable to proceed.
3. `ConnectingWifi` - retain firmware ownership, show the cancellable Leaf Log screen, and connect
   only to saved station networks; never start the Leaf access point.
4. `WaitingForTime` - obtain valid network time for TLS without blocking the charging loop.
5. `Uploading` - stream one IGC at a time to `/api/ingest` without loading the entire file into heap.
6. `RecordingResult` - atomically add the returned Leaf Log flight ID to the logbook JSON.
7. `Finishing` - close SD files, disconnect Wi-Fi, present the medium if a data host is connected,
   and allow charging sleep again.
8. `HostOwned` - perform no firmware filesystem access while the medium is presented.
9. `WaitingForEject` - detect explicit host eject and return to eligibility checking for any
   remaining flights.
10. `Backoff` - on a power-only charger, leave files unchanged after transient failures and retry
    later without remaining continuously awake. If a data host is waiting, present mass storage
    instead of holding it through a long backoff.

The state machine must expose whether it needs Leaf to stay awake. Charging sleep should require both
the existing diagnostic-network sleep condition and `leafLogSync.canSleepWhileCharging()`.

While the Leaf Log sync screen is active, intercept any short button press before normal charging-page
behavior and set a cancellation request without immediately exposing the medium. The state machine
must first abort or finish the current bounded HTTP/storage operation, close every file, and leave any
atomic JSON replacement in a recoverable state. Only then may it call `mediaPresent(true)`. The UI may
briefly show `Opening USB drive...` during this handoff. Normal charging-page button behavior resumes
after mass storage is presented.

## SD card ownership

Separate USB connection state from SD ownership. A connected or suspended data host does not own the
filesystem until firmware presents the medium, while presenting the medium is an irrevocable handoff
until host eject or disconnect.

The ownership states should be approximately:

```text
FirmwareReserved -> FirmwareUploading -> HostOwned
       |                    |                 |
       +-- no work ---------+                 +-- explicit eject --> FirmwareReserved
       +-- cancel ----------+
       +-- failure ---------+
```

Configure MSC with `mediaPresent(false)` while ownership is `FirmwareReserved` or
`FirmwareUploading`. USB may enumerate normally, but MSC reads and writes must not reach the SD card.
After all firmware file handles and transactions are closed, `mediaPresent(true)` transitions to
`HostOwned`.

Register `USBMSC::onStartStop()` and treat only an explicit eject (`start == false` with
`load_eject == true`) as a host-to-firmware handoff. USB suspend and stop/spindown commands without
`load_eject` do not release the filesystem. After an eject-triggered upload, keep the medium ejected
until USB disconnect; the first version does not re-present it during the same USB session.

An exclusive firmware transaction API in `SDCard` is still useful for enforcing these charging-mode
transitions. It should prevent `mediaPresent(true)` while firmware files are open and prevent the
Leaf Log sync and other charging-mode work from accessing the filesystem in `HostOwned`. Atomics
should communicate cancellation and USB events safely between callback and task contexts. Expanding
that guarantee across the transition to normal powered-on operation belongs to the separate
power-on ownership backlog.

Do not reclaim the medium after an inactivity timeout. A mounted FAT filesystem may retain cached
state and open files indefinitely, so lack of recent MSC callbacks is not an ownership release.

## Eligible logbook entries

For each `/logbook/*.json` file, require:

- A valid logbook JSON object.
- `track.saved == true`.
- `track.format == "igc"`, or a track path ending in `.igc` for compatible older entries.
- A normalized track path contained under `/tracks`.
- An existing non-empty IGC no larger than 5 MB.
- No existing `leaf_log.flight_id`.
- No terminal `leaf_log.rejected` reason.

The local `pilot.id` does not affect upload routing. Every eligible flight on the device is uploaded
to the Leaf Log account that owns the device token. KML tracks and orphan IGC files remain
`NotApplicable`. A locally invalid IGC entry is not uploaded, but it must be assigned a durable
terminal rejection so the user can see that Leaf considered it and why it could not be delivered.
This includes malformed logbook data, unsafe track paths, missing track files, empty track files, and
oversized track files. Directory iteration can remain unsorted, and the uploader continues through
the backlog while uploads or local classification succeed.

Syntactically broken JSON that cannot appear in either logbook UI remains untouched and is skipped;
do not create a sidecar or rejection index for it. For parseable JSON, classify in a deterministic
order: invalid schema, no saved IGC intended, unsafe or missing track path, missing file, empty file,
then oversized file. `track.saved == false` and a valid KML reference are `NotApplicable`, not
failures. A saved track with no usable path is `invalid_logbook`.

## Recording delivery

Keep the per-flight upload record minimal. On any valid HTTP 200 response, including `deduped: true`
or `status: "failed"`, preserve the existing logbook document and add only:

```json
"leaf_log": {
  "flight_id": "tn8t"
}
```

The presence of `leaf_log.flight_id` is the authoritative delivered marker. Leaf does not persist the
server status, dedupe result, or upload timestamp because they do not affect future device behavior.

A terminal local rejection in a parseable logbook document uses the same object with one mutually
exclusive reason:

```json
"leaf_log": {
  "rejected": "too_large"
}
```

Initially supported reason codes are:

- `invalid_logbook` - parseable JSON is not a valid Leaf logbook object or lacks the fields needed
  to identify its saved track.
- `unsafe_track_path` - the normalized track path is not contained under `/tracks`.
- `missing_track` - the referenced IGC file does not exist or cannot be opened.
- `empty_track` - the referenced IGC file has zero bytes.
- `too_large` - the referenced IGC exceeds 5 MB locally, or Leaf Log returns HTTP `413`.
- `invalid_igc` - Leaf Log returns HTTP `400` for the submitted bytes.

`rejected` is written only for a permanent condition and suppresses automatic retries. These codes
are persisted rather than collapsed into a generic failure so both UIs can explain the result. Repair
tooling or a manual JSON edit may remove the rejection to retry that flight; a later successful upload
replaces it with `flight_id`.

Update the JSON using a temporary file plus backup/rename replacement, preserving unknown fields.
This should follow the existing atomic profile-writing pattern. A power loss must leave either the
old entry or the complete updated entry recoverable. Document the optional `leaf_log` object in the
logbook YAML schema.

## Per-flight Leaf Log status

Expose the persisted result when browsing logbook entries. Derive one of four states:

- `NotApplicable` - a valid entry does not claim a saved IGC track, such as a KML entry or
  `track.saved == false`; show no Leaf Log status.
- `NotUploaded` - eligible IGC with neither `flight_id` nor `rejected`.
- `Uploaded` - `leaf_log.flight_id` is present.
- `Rejected` - `leaf_log.rejected` is present.

When the Leaf Log Labs setting is enabled, the Leaf web app logbook entry card should show a compact
icon plus accessible phrase: `Not uploaded to Leaf Log`, `Uploaded to Leaf Log`, or
`Leaf Log upload rejected`. A rejected entry should also show a short reason mapped from the stable
code: `Invalid logbook`, `Unsafe track path`, `Track file missing`, `Track file empty`, `Too large`,
or `Invalid IGC`. Use a tooltip or accessible label for icons rather than relying on color alone.

Add the same state to the on-device Logbook screen in the upper-right corner, immediately to the
right of the `/ LOGBOOK \` title tab. Use the Leaf glyphs in `leaf_8x14`: an empty leaf (141) for
`NotUploaded`, a solid leaf (140) for `Uploaded`, and an exclamation leaf (143) for `Rejected`.
Code point 142 is reserved for a future state with explicit semantics.
`NotApplicable` shows no glyph. Keep a stable icon-sized area so paging between entries does not move
the title or other content.

Extend `LogbookEntrySummary` and `LogbookStore::readSummary()` to parse this state once. Reuse that
summary in the shared device logbook card and include the state/rejection reason in the web app
logbook-entry JSON response. Do not re-open the JSON separately in each UI.

## Error behavior

- Network error or HTTP `5xx`: leave the entry unchanged and retry with backoff.
- HTTP `401`: stop the batch, erase the invalid token from NVS, persist
  `reconnect_required`, and do not retry every file.
- HTTP `400`: record `invalid_igc`; HTTP `413`: record `too_large`, so the same immutable file is not
  attempted during every charging session.
- Invalid success JSON or a missing `flightId`: treat as transient and do not mark delivered.
- User cancellation: safely close network and filesystem work, then present mass storage.
- Data host connected while firmware owns the unpresented medium: continue the cancellable upload;
  the host waits with no MSC medium until completion, cancellation, or bounded failure.
- USB suspend after the medium is presented: retain `HostOwned`; do not upload.
- Explicit host eject: reacquire firmware ownership and resume eligible uploads.

When a data host is waiting for mass storage, the first transient network or server failure stops the
batch and presents the medium. On a power-only charger, retry after approximately 5, 15, and 60
minutes, sleeping normally between attempts. Continue through the entire backlog while uploads are
succeeding.

### Network timeout defaults

Use these initial defaults:

- Saved Wi-Fi connection: 5 seconds per saved network, with the existing maximum of three networks
  and a 15-second total connection window.
- Network time synchronization: 8 seconds total.
- DNS/TCP/TLS connection for a Leaf Log request: 10 seconds.
- Established socket inactivity: 15 seconds without successful read or write progress.
- Total elapsed time for one IGC upload, including its server response: 60 seconds.
- Pairing start/poll HTTP calls: the same 10-second connection and 15-second inactivity limits.

Implement the total upload deadline outside `HTTPClient`; `setConnectTimeout(10000)` and
`setTimeout(15000)` provide the underlying connection and socket-idle bounds. Stream IGC data in
chunks and check cancellation between chunks. Normal button cancellation should hand off to mass
storage within about one second; a call already blocked in the network stack may take up to the
10-second connection or 15-second inactivity timeout. No individual blocking operation should exceed
those bounds.

## Device pairing and token

Leaf uses one current Leaf Log pairing and bearer token per physical device, not one token per local
pilot profile. The paired Leaf Log account owns all flights automatically uploaded from that device,
regardless of the optional pilot snapshot stored in an individual logbook entry.

This matches the expected use of a Leaf unit as a primarily personal device. If it is temporarily
loaned to another pilot, the resulting flight still uploads to the device owner's account. Future
Leaf Log website features may assign, share, or move flight records between accounts; that workflow
is outside firmware upload routing.

Firmware changes implied by this decision:

- Do not require a pilot selection before starting Leaf Log pairing.
- Do not filter pending logbook entries by `pilot.id`.
- Treat a newly paired token as the device's single current credential and replace the locally stored
  token when the device is paired again.
- Remove or deprecate `leaf_log.pilot_id` in the profiles schema, validation, empty profile document,
  and Leaf Log linking UI. It is not part of authentication or upload ownership.
- Keep logbook pilot snapshots unchanged for flight display and future website reassignment features.

Store the bearer token in a dedicated device NVS namespace in the first auto-upload release; do not
leave it in `/profiles/profiles.json` where USB mass storage can expose it. No migration is required
for experimental profile-file tokens; test devices may simply be paired again. A normal settings
reset and every equivalent reset performed by `factory_interface` must erase the Leaf Log token and
pairing state. Replacing or removing the SD card must not remove a valid device pairing.

Disabling `Settings > Leaf Labs > Leaf Log` stops automatic uploads but retains the device token and
pairing state in NVS. Re-enabling the setting resumes uploads without requiring another pairing. This
also keeps the token behavior compatible with eventually removing the experimental setting and
gating uploads only on whether a token is present.

Leaf Log has a public, unique `handle` and a public, non-unique `displayName`. Return both as an
`account` object when pairing succeeds, and cache them with the device pairing in NVS. Do not return or
store the account email. Leaf may display `Linked to Display Name (@handle)` in its web app. Treat these
fields as a cosmetic snapshot rather than authentication or upload-routing data because the user may
change either field later on Leaf Log.

The first version does not need a synchronization dashboard in the Leaf Log settings card. Keep the
card limited to actionable pairing state and controls: `Not linked`, `Linked`, or
`Reconnect required`, plus the appropriate pair/re-pair action. Pending-flight counts, last-success
timestamps, and rejection summaries would add SD scans and UI complexity without improving the normal
workflow. Show scan/upload progress on the charging screen; uploaded flights are visible in Leaf Log.

When linked, the Leaf web app card should also offer `Unlink`. Gate it with the same style of
`Are you sure?` confirmation used when deleting a logbook entry. On confirmation, attempt to revoke
the current token on Leaf Log, then erase the local token and pairing state even if the network call
fails. A failed server cleanup leaves the old token visible for manual revocation in Leaf Log.

### Leaf Log contract changes

Update `leaf-log/docs/device-api-contract.md` and related Leaf Log planning documentation to state:

- One current pairing/token is stored per Leaf device, not per on-device pilot profile.
- Every IGC sent with that bearer token is owned initially by the Leaf Log account associated with
  the token. Local Leaf pilot IDs do not select the destination account.
- The firmware sends only the IGC and existing upload headers; no pilot identifier is required.
- Assignment, sharing, and transfer between Leaf Log accounts are later website capabilities and do
  not change the device ingest request.

Make additive changes to both successful response shapes. A claimed pairing poll returns the token and
public account identity:

```json
{
  "status": "claimed",
  "token": "llk_...",
  "account": { "handle": "skyhawk", "displayName": "Jamie Smith" }
}
```

Every successful `/api/ingest` response also includes the same `account` object. After durably recording
the returned `flightId`, Leaf updates its cached handle and display name if they changed. This refreshes
the label naturally on the next upload without adding another request or connecting to Wi-Fi solely for
cosmetic metadata. If no new flight is uploaded, the cached label may remain stale until the next upload
or re-pairing; this is acceptable because the bearer token, not the label, determines ownership.

Ordinary re-pairing creates a new server token. If Leaf only overwrites its local NVS value, the old
token remains valid and visible in Leaf Log's device settings. This is not account access - device
tokens can only upload flights - but a copied old token could continue adding flights, and repeated
re-pairing would leave confusing duplicate device credentials.

Decision: implement a bearer-authenticated self-revoke endpoint, initially
`POST /api/devices/revoke-self`. During re-pairing, Leaf retains the old token in memory, completes
the new pairing, durably stores the new token, and only then calls the self-revoke endpoint using the
old token. Failure to revoke the old token does not roll back the new pairing; Leaf Log continues to
show the old credential so the user can revoke it manually. An already revoked old token returning
`401` is harmless.

Self-revoke handles normal re-pairing without transmitting a permanent hardware identifier. It cannot
clean up a token erased by settings reset, factory reset, corruption, or loss. Those credentials remain
manually revocable on the website. Guaranteeing one live token per physical device even after local
credential loss would require a stable server-visible device identifier and decisions about privacy,
device transfer between accounts, and whether that identifier survives settings reset. That stronger
identity model is not required for the first auto-upload implementation.

### Leaf Log device-token management UI

Improve Leaf Log's Connected Devices list so an account with multiple Leaf units and stale tokens can
make an informed manual revocation decision. Each token row should show:

- The device label and active/revoked state.
- Pairing creation date and exact local time, not date alone.
- Last successful device upload date and exact local time, or `Never used`.
- The latest flight delivered with that token, including a link and a recognizable flight summary.
  Leaf Log may choose a compact track preview or a simpler summary according to the available screen
  width; that responsive presentation does not need to be decided in the device-upload plan.

Do not label a token `Current` based only on creation or last-used time; Leaf Log cannot know which
plaintext token a device currently retains after resets or failed cleanup. The richer timestamps and
latest-flight context are evidence for the user, not a server claim of current ownership.

Leaf Log currently records `lastUsedAt` after successful ingest but does not retain the corresponding
flight on the token. Add a nullable latest-flight association to `DeviceToken`, such as
`lastFlightId`, and update it together with `lastUsedAt` after every successful `/api/ingest`
response, including a deduplicated upload. Keep the association when the token is revoked. If the
flight is later deleted or becomes unavailable to that account, clear or omit the link safely.

Existing tokens and historical uploads may have no latest-flight association; display the available
creation/last-used metadata without attempting an unreliable backfill.

## Suggested implementation boundaries

- `src/vario/comms/leaf_log_client.*` - shared base URL, CA certificate setup, pairing requests, and
  streaming IGC upload.
- `src/vario/comms/leaf_log_sync.*` - charging state machine, eligibility, scan, retry, and result
  handling.
- `src/vario/storage/sd_card.*` - exclusive firmware/MSC ownership coordination.
- `src/vario/logbook/` - eligible-entry scanning and atomic upload-result patching.
- `src/vario/taskman.*` - call the state machine and include it in the charging sleep decision.
- A dedicated NVS-backed Leaf Log credential store - device pairing status and token access, including
  reset handling shared with firmware settings reset and `factory_interface`.

Extract the existing Leaf Log HTTPS constants and TLS setup from `webserver.cpp` rather than
duplicating them in the uploader.

## Test matrix

- Leaf Labs setting off: no Wi-Fi attempt and no logbook access.
- No token: no Wi-Fi attempt and no upload.
- Firmware settings reset and `factory_interface` settings reset: erase the device token and pairing
  state.
- Existing experimental token only in `profiles.json`: do not migrate it; require re-pairing.
- Power-only charger, valid token, saved Wi-Fi: eligible flights upload and are marked.
- Computer connected with Leaf Log disabled or unpaired: mass storage appears immediately.
- Paired computer connection with no pending flights: mass storage appears after a quick scan.
- Paired computer connection with pending flights: medium remains absent, upload status and cancel
  action appear, and mass storage is presented after completion.
- Cancel during Wi-Fi, NTP, IGC streaming, and result recording: reach a safe transaction boundary,
  close files, then present mass storage.
- Network or server failure with a host waiting: fail open and present mass storage.
- Host eject after a cancelled or partial batch: safely resume remaining uploads.
- Suspended computer after mass storage was presented: retain host ownership and do not upload.
- MSC inactivity for any duration: retain host ownership and do not upload.
- Exact duplicate on Leaf Log: `deduped: true` still records the returned `flightId`.
- Leaf Log returns `status: "failed"` with HTTP 200: record delivery and do not retry.
- Revoked token (`401`): stop batch and surface reconnect-required state.
- Server `5xx` or network loss with a host waiting: retain unuploaded state and present mass storage.
- Server `5xx` or network loss on a power-only charger: retry with 5/15/60-minute backoff.
- Syntactically valid but schema-invalid logbook object: record `invalid_logbook` in the entry and
  show the failed icon and reason.
- Syntactically invalid logbook JSON: leave it untouched, create no auxiliary status file, and skip
  it because it cannot appear in either logbook UI.
- Unsafe track path, missing track, or empty track: record the corresponding terminal rejection,
  show its failed icon and reason, and continue with later entries.
- Oversized or server-rejected IGC: record `too_large` or `invalid_igc` and continue with later
  entries.
- Logbook entries containing different or missing local pilot IDs: upload all otherwise eligible
  entries to the account associated with the device token.
- Re-pairing: replace the locally stored device token and use the new account for future uploads.
- Re-pairing with self-revoke: save the new token before revoking the old one; cleanup failure leaves
  the new pairing active and the old token manually revocable.
- Disable and re-enable the Leaf Log Labs setting: retain the pairing token and resume without
  re-pairing.
- Unlink in the Leaf web app: require confirmation, attempt self-revocation, and erase local pairing
  state even when server cleanup fails.
- Leaf Log Connected Devices: show exact creation and last-used times, and the latest associated
  flight/track where available.
- Deduplicated device upload: update the uploading token's last-used time and latest-flight
  association to the existing flight.
- Historical token without a latest-flight association: render available metadata without failure.
- Wi-Fi, NTP, connect, socket-idle, and total-upload timeout boundaries: fail predictably and preserve
  cancellation and mass-storage handoff.
- Web app logbook card: show accessible `Not uploaded`, `Uploaded`, and `Rejected` states.
- Device logbook card: show a compact status for the same states without overlapping existing fields.
- Pairing response: cache the returned public handle and display name, and never expose account email.
- Upload after changing the Leaf Log profile: refresh the cached handle/display name from the successful
  ingest response without affecting token ownership.
- KML entry and orphan IGC: remain `NotApplicable` and are not uploaded.
- Power loss during JSON replacement: recover the original or complete updated entry.
- Large backlog: remain responsive, avoid watchdog resets, and continue while uploads succeed.

## Open decisions

No product decisions are currently open for the first auto-upload implementation.
