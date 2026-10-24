---
title: USB Mass-storage Ownership During Power-on Backlog
description: Implemented firmware/USB SD ownership and remaining hardware qualification.
---

# USB Mass-storage Ownership During Power-on Backlog

Status reconciled **2026-10-24**: the selected ownership and power-on behavior is implemented
in `storage/sd_card.*`, `comms/leaf_log_sync.*`, `power.cpp` and `taskman.cpp`.
Charging dialogs and cancel confirmation were updated in `18fc69af` (#425). Phase 1 logbook
transaction coordination and bounded native USB diagnostics merged in `605810eb` (#430).
This is no longer an undecided product design or an unimplemented Leaf Log follow-up.

## Implemented behavior

- Powered-on Leaf retains firmware SD ownership. Plugging USB into an operating Leaf provides
  power/charging without presenting the SD mass-storage LUN.
- Turning Leaf off while plugged in closes flight work, enters charging, starts an eligible
  Leaf Log session and presents mass storage after completion or confirmed cancellation.
- During upload activity, one short center press shows `Cancel Uploads?`; a second within
  five seconds confirms cancellation to USB. Directional presses do not cancel. Holding center
  requests power-on directly and cancels active transfer work.
- If mass storage is presented, holding center requests deliberate forced removal:
  reject new host operations, drain active MSC callbacks, stop raw host access, remount FAT,
  then continue power-on. `Closing USB Drive` appears when a host has actually enumerated;
  a power-only connection shows `Turning On`.
- Firmware filesystem access is gated by ownership. Diagnostics pause while the host owns the
  card. Raw MSC reads/writes require host ownership and active-callback accounting.
- Explicit eject or USB disconnect returns ownership; suspend, inactivity and stop/spindown
  without `load_eject` do not. An eject-triggered upload keeps the medium ejected afterward.
- Failed USB presentation shows `USB Drive Not Ready` temporarily and supports a center-click
  handoff retry. Power-on can still be requested.

The authoritative states are `FirmwareReserved`, `FirmwareUploading`, `TransitioningToHost`,
`HostOwned` and `TransitioningToFirmware`. The historical concurrent FAT/raw-block behavior
described by the original proposal has been replaced; it is not the intended current behavior.

## Ownership limits

A deliberate center hold cannot flush writes still cached by the computer. Eject before
power-on remains preferable when the host has mounted the drive. Firmware must never infer
release from inactivity or revoke a mounted medium in the background.

The alternatives of requiring eject before power-on or allowing normal operation without SD
access were not selected. Power-on waits for the firmware acquisition path; it does not proceed
with simultaneous firmware/host filesystem access.

Phase 1 adds checked logbook/result saves, recovery and transaction coordination at ownership
transitions. The broader all-writer lifecycle/durability audit and physical power-interruption
qualification remain in [SD card write robustness](sd-card-write-robustness.md).

## Verification and remaining work

`sim/tests/leaf_log_sync.cpp` and `sim/tests/charging_dialogs.cpp` cover ownership decisions,
cancel/hold handling, draining active callbacks, handoff failure, ejection and display behavior.
The [charging-dialog capture record](../../ui-reviews/charging-dialog-revamp/verification.txt)
records the #425 simulator checks. Host-directory mocks do not qualify physical USB/FAT behavior.

During #430 hardware testing, an open USB serial monitor exposed a charge-to-on freeze caused
by diagnostic writes waiting on a full transmit FIFO after detach. Native USB output is now
bounded and best effort, and disabled before software detach. Focused stalled-endpoint tests
passed; one monitored startup cycle reached normal operation on Leaf 3.2.8. Repeat cycles and
the broader matrix below remain qualification work.

Retain the following device matrix for qualification:

- Power-only charger and an enumerated computer, with Leaf Log enabled/disabled.
- Mounted-drive center hold, including host I/O already in flight and cached host writes.
- Explicit eject/disconnect before and during a pending request; repeated eject commands.
- USB suspend, inactivity and stop/spindown without eject: retain host ownership.
- SD removal, remount failure and handoff retry during each transition.
- Repeated operating/charging/upload/USB transitions, ensuring flight logging and other firmware
  storage users resume only with firmware ownership.
