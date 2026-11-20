---
title: Move blocking shutdown outside message dispatch
description: Assess and remove a shutdown lock dependency shared by UDP injection and the FANET receive worker.
---

# Move blocking shutdown outside message dispatch

Status: proposed, not implemented. Identified during the November 19, 2026 full-card tests.
Keep this architectural change separate from checked user-file saves and startup storage warnings.

## Evidence and scope

With UDP replay active, shutdown printed `Lock acquisition failed in MessageBus::receive` and
restarted. With replay stopped, otherwise comparable shutdowns completed. The saved trace is
`test-data/field-testing/sd-full/2026-11-19-3.2.8/21-flight-shutdown.serial.txt`.

The lock dependency is not exclusive to the developer replay interface:

- `MessageBus::receive` holds its recursive mutex while dispatching to subscribers. Other threads
  wait up to 3 seconds for that mutex (`src/vario/dispatch/message_bus.h`).
- Button dispatch can call `Power::shutdown` synchronously from a subscriber. Shutdown includes
  file finalization, worker cleanup, sounds and delays.
- UDP callbacks publish directly to the bus from another task.
- The production FANET receive worker also publishes directly to that bus
  (`src/vario/comms/fanet_radio.cpp`, `processRxPacket`).
- Shutdown stops FANET as part of flight cleanup. Its resource-release path waits for workers to
  park. A worker already waiting to publish cannot park until the bus becomes available, creating
  a dependency cycle with the button handler. Stop deadlines can expire before that resolves.

Live FANET traffic has not reproduced this failure in this investigation. The source establishes
a shared architectural risk; it does not establish how often normal users encounter it. The
current storage test unit reported FANET off/uninstalled.

## Recommended design

1. Turn shutdown calls from button handlers into a pending request, preserving the low-battery
   reason, consuming the initiating button, and making repeated requests harmless.
2. Execute the blocking transition from the main loop after message dispatch releases the bus
   mutex. Keep the existing shutdown deadline/watchdog protection and charging recovery semantics.
3. Stop new asynchronous publications and quiesce admitted producers outside the bus lock before
   dismantling their resources. Account for callbacks that passed their acceptance check already;
   changing a boolean alone is insufficient for those callbacks.
4. Audit other blocking transitions invoked by subscribers for the same dependency. Do not fix
   this by simply lengthening the bus timeout or deleting tasks while they can hold shared locks.

No new thread or large queue is inherently required for a deferred shutdown request. Exact
state changes, lock ordering, memory impact and low-battery timing still need implementation review.

## Verification

- Deterministic concurrent publisher versus shutdown tests, including an already-admitted publisher.
- Continued UDP replay during shutdown on writable and full cards.
- Hardware FANET reception during shutdown, with Bluetooth connected as a combined workload.
- Normal, low-battery, repeated-request, stuck-worker, watchdog and charging-handoff regressions.
- Confirm the main loop retains its shutdown bound and the exact shutdown reason survives deferral.

Until then, storage automation must stop/join its replay producer and disable injection before
requesting power-off. This is a test workaround, not a firmware architectural fix.
