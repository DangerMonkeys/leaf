---
title: USB Mass-storage Ownership During Power-on Backlog
description: Decide how Leaf should enter normal powered-on operation while a computer owns the SD card through USB mass storage.
---

# USB Mass-storage Ownership During Power-on Backlog

Status: product behavior selected; implementation in progress on the Leaf Log ownership follow-up.

## Problem

When Leaf boots from USB power without a center-button press, it enters `PowerState::OffUSB` and
presents the SD card as writable USB mass storage. A computer may mount the FAT filesystem, retain
open files, and cache writes.

The center button can then switch Leaf into normal powered-on operation without requiring the host
to eject the volume. The firmware continues to use the mounted filesystem while USB MSC callbacks
can read and write the same card at the raw-block level. This is the device's current behavior, but
it does not provide exclusive filesystem ownership and can risk filesystem corruption.

USB suspend, a stop/spindown command, or a period without MSC activity does not prove that the host
has released the filesystem. Only an explicit eject or USB disconnect is a safe ownership handoff.

## Selected behavior

- Normal powered-on operation always retains exclusive firmware ownership of the SD card. Plugging
  USB into an operating Leaf provides power and charging but does not present the SD LUN.
- Turning Leaf off while it remains plugged in closes firmware files, enters charging mode, runs a
  fresh Leaf Log upload session when enabled, and presents mass storage afterward.
- Holding center during Leaf Log activity cancels directly into powered-on operation without first
  presenting the SD card. A short center press cancels to USB mass storage; directional presses do
  not interrupt Leaf Log.
- Holding center after mass storage is already present deliberately removes the SD medium, rejects
  new host operations, drains in-flight MSC callbacks, remounts the filesystem, and powers on.
  Because a USB device cannot flush unsent host caches, this last transition remains a deliberate
  forced-removal operation and should be treated as uncommon.
- Firmware performs no filesystem access while the host owns the SD card. SD-backed diagnostics are
  paused rather than buffered or written concurrently.

## Scope

Resolve what Leaf should do when the user requests normal powered-on operation while the USB host
owns the SD card. This is intentionally separate from the Leaf Log auto-upload work. The first Leaf
Log auto-upload release may coordinate SD ownership while Leaf remains in USB charging mode, but it
must preserve the existing power-on behavior rather than introduce a new restriction at the same
time.

The eventual solution should cover:

- The center-button transition from `PowerState::OffUSB` to `PowerState::On` while the MSC medium is
  presented.
- What the on-device UI tells the user when a host eject or disconnect is required.
- Whether normal operation may continue with SD-dependent features unavailable.
- Track logging, logbook writes, profiles, firmware updates, diagnostics, and every other firmware
  filesystem user while the host retains ownership.
- Reacquiring firmware ownership after explicit eject or disconnect.
- Re-presenting the medium later in the same USB session, if supported.

## Product approaches to evaluate

### Require eject before power-on

Keep Leaf in charging mode and show an instruction such as `Eject USB drive to turn on`. Complete
the power-on transition only after explicit host eject or disconnect.

This provides the clearest ownership boundary, but changes the current center-button behavior and
may surprise a user who expects Leaf to turn on immediately.

### Power on without SD access

Allow the power-state transition while the host retains ownership, but prevent all firmware
filesystem access until eject or disconnect. The UI must clearly show that logging and other
SD-dependent features are unavailable.

This preserves immediate power-on, but requires auditing and gating all SD users and defining how
each affected feature behaves without storage.

### Prompt for an explicit choice

Ask the user to eject the drive or deliberately continue with storage unavailable. This makes the
tradeoff visible but adds a new transition UI and still requires the no-SD operating mode.

Leaf must not revoke a mounted medium in the background or infer ownership release from MSC
inactivity. The explicit center-hold power-on action is the only deliberate forced-removal path.

## Decisions resolved

- Powered-on operation does not expose the SD LUN.
- Power-on never proceeds without firmware SD ownership.
- A center hold is a deliberate request to reclaim the card and continue power-on automatically.
- Returning to charging mode starts Leaf Log before presenting mass storage.

## Suggested implementation boundaries

- `src/vario/storage/sd_card.*` - authoritative firmware/host ownership state and guarded handoffs.
- `src/vario/system/usb_state.*` - USB connection, suspend, resume, and disconnect events.
- `src/vario/ui/display/pages/primary/page_charging.*` - power-on request and user guidance.
- `src/vario/power.*` and `src/vario/taskman.*` - defer or constrain the transition to normal
  operation.
- All direct `SD_MMC` users - audit or route through an ownership-aware transaction API if normal
  operation without SD access is selected.

## Test matrix

- Power-only charger: center-button power-on remains immediate.
- Computer with the drive mounted: the selected behavior is deterministic and never permits
  concurrent host and firmware filesystem access.
- Explicit host eject before power-on: firmware reacquires the card and powers on normally.
- Explicit host eject after a pending power-on request: the documented automatic or second-press
  behavior occurs.
- USB disconnect with a pending power-on request: firmware safely reacquires the card.
- USB suspend or MSC inactivity: host ownership is retained.
- Stop/spindown without `load_eject`: host ownership is retained.
- Removal or failure of the SD card during the transition: Leaf fails safely and reports the missing
  storage state.
- Track logging and every other storage-dependent feature remain disabled until firmware ownership
  is established.
