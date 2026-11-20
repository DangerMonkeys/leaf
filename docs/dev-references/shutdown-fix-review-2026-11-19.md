---
title: Deferred shutdown and publisher drain, November 19, 2026
description: Shutdown lock fix, preserved deadline and watchdog behavior, simulator fault tests, and replay-active 3.2.8 hardware results.
---

# Deferred shutdown and publisher drain

Shutdown cleanup now runs from the main loop after the requesting message-bus callback
returns. The fix addresses the lock dependency observed during UDP replay and shared
with production FANET reception. It preserves the existing shutdown sequence, save
opportunity, power-latch behavior and watchdogs. The storage changes were committed
first as `45e599af`; this change is a separate follow-up on `codex/usb-bench-console`.

## Implementation and preserved behavior

- `Power::shutdown()` is now a **loop-core request**, with no SD write, driver cleanup or
  blocking wait. It consumes the initiating button and arms the existing shutdown guard
  immediately. Repeated requests do not extend the deadline. A low-battery request made
  before execution takes precedence over an ordinary power-off request.
- `TaskManager::update()` services a pending request before other loop work, after dispatch
  has returned and released its bus mutex. No worker, queue or additional stack is added;
  the task slot schedule and exported runtime labels remain unchanged.
- Before taking the logbook/file lock, shutdown closes UDP callback admission and waits
  for already-admitted callbacks to finish. It also stops FANET producer admission and
  waits for RX/TX worker parking through the existing bounded stop logic. Nothing is
  deleted while a producer can still access it. A UDP drain timeout of 500 ms or the
  existing FANET stop timeout uses guarded `WorkerTimeout` recovery.
- UDP injection stays suspended while charging, without changing the saved opt-in. It
  can resume after an intentional wake with the usual developer/network conditions.
  The listener is retained; this does not repeatedly close/rebind the SDK's UDP socket.
- After draining, the existing flight and Stack saves, settings save, off/low-battery
  splash, BLE quiescence, sound, peripheral sleep, latch release, BLE teardown and charging
  handoff run in their existing order. The existing delays and shutdown save retry
  policy are unchanged.

The **8-second deadline starts at the request**, so time spent waiting to leave dispatch
or reach the main loop is included. The **10-second independent center-button hold**,
**10-second RTC backstop**, emergency latch release, reset stages and retained recovery
marker remain in place. New diagnostic stages (`requested` and `publishers`) are appended,
preserving every previous retained stage ID. The hardware guard implementation changes
only to label those new stages; its timing and emergency path are unchanged.

## Verification

The final combined simulator run passed:

```sh
make -C sim -j1 test-shutdown test-shutdown-bus test-udp-injection test-logbook-io test-flight-auto-start test-thermal-lifecycle test-checked-storage test-leaf-log-sync
```

Run these targets sequentially because UDP suites share port 7431. Compilation can still
be parallelized separately.

| Test | Result |
| --- | --- |
| Subscriber requests shutdown while holding the real bus mutex | Request returns with flight/power still active; an admitted UDP callback waits for that mutex |
| Deferred execution | Bus is released, callback drains, flight saves, charging succeeds, and injection resumes on a later wake |
| Stuck admitted callback | Guarded WorkerTimeout recovery releases the latch within 510 ms; resources are not deleted underneath it |
| Unserviced/repeated request | Deadline still expires about 8 seconds from the first request |
| Low-battery upgrade | Later ordinary requests cannot erase it; IGC ends with `CAUSE=LOW_BATTERY` |
| Normal and low-battery shutdown | Flight and Stack save, latch releases, charging completes inside the deadline |
| Existing guard faults | Lock, file-save, worker, sound, peripheral, display/latch and late BLE stalls still release the latch at 8,004 ms in the simulator |
| RTC/interrupt faults | RTC fallback at 10,004 ms; nested cleanup does not extend the outer deadline; held-center recovery/release behavior passes |
| Related regressions | Logbook fault/retry bounds, flight start/stop and IGC causes, thermal lifecycle, checked storage/startup warnings and Leaf Log sync pass |

The thermal test initially failed because it expected an undated placeholder filename
to remain after GPS time acquisition and finalization. Readback showed the correct dated
entry with the same flight ID. The test now uses a fresh fixture and follows that ID,
then validates its end data and track association. No logbook rename behavior was changed.
Both the initial assertion and the final passing logs are retained.

## Plugged-in Leaf 3.2.8

Both OTA app slots were flashed and verified. The app-mode COM port first nudged the
unit into its Espressif boot port; the automatically detected retry succeeded.

- A short flight was started with stationary synthetic GPS at 5 Hz and physical pressure
  and motion sensors left active. Host replay **continued through the normal power-off
  hold and the subsequent charging check**. The host did not disable injection first.
- The Leaf returned to charging without a reboot. The flight IGC contains
  `CAUSE=POWER_OFF`, has a complete logbook entry, and passes checksum validation.
- An already-running Stack capture saved on shutdown: 114-byte CSV, 203.373 seconds,
  loop minimum unused stack 7,868 B. BLE/FANET workers were absent from that capture;
  those zero-instance rows are not measurements of their stack headroom.
- The same boot then woke, reopened the Web App with injection still opted in, received
  fresh replayed GPS, and completed another replay-active shutdown to charging.
- Two earlier button sequences selected a display field instead of requesting shutdown;
  their flights auto-stopped normally. These were test-driver navigation errors, not
  failed shutdown requests. Those two IGCs also passed checksum validation. Their traces
  remain numbered `06`–`10`; successful shutdown evidence is `12`–`20`.
- Original settings and both original SD files were restored, with matching SHA-256.
  The unit is charging with the new candidate installed and 8,503,296 bytes free.

The final cleanup shutdown also reached charging with replay disabled. Minimum heap did
not reset across the tested cycles. The lowest boot-wide reading captured was 27,612 B
and final free internal heap was 178,012 B; these are combined lifecycle observations,
not an isolated memory comparison or a worst-case reserve guarantee.

## Size and exact identity

| Linked metric | Storage-fix bench | Shutdown-fix bench | Change |
| --- | ---: | ---: | ---: |
| Static RAM | 86,604 B | 86,620 B | +16 B |
| Flash | 2,419,384 B | 2,420,012 B | +628 B |

No new task or retained recording buffer is allocated. The added state consists of a
small pending request and a callback admission counter/barrier.

- Environment: `leaf_3_2_8_bench`; reported firmware `0.2.4-45ed.bench+h3.2.8`.
- Device MAC: `7C:4F:AD:3B:AC:B0`.
- Firmware binary: 2,420,656 bytes.
- SHA-256: `4d010ecb533194ab323499a1df58d7114e35eabdef20d50b2c12e7ce80170e53`.
- Evidence: `test-data/field-testing/shutdown/2026-11-19-3.2.8/` contains exact source and
  build hashes, flash/build/format logs, passing and diagnostic simulator logs, USB/HTTP
  traces, producer timestamps, SD snapshots, IGC validation and local test drivers.

## Remaining coverage and scope

This unit reported FANET off/uninstalled; there was no connected Bluetooth phone.
Live FANET reception plus connected Bluetooth during shutdown remains a high-value
hardware check. The simulator models the lock interaction with a real asynchronous UDP
publisher and bus mutex; it does not model the physical radio or NimBLE stack.
This session did not repeat the zero-space hardware workload on the shutdown candidate;
logbook/checked-storage failure bounds remain covered by the combined simulator suites
and earlier storage hardware results. This is not release qualification.

The fix moves **Power shutdown** outside dispatch. Other blocking subscriber actions,
such as manual flight stop, should be audited separately. The stale FAT32 free-count
finding remains independent; this patch makes no filesystem metadata/cache changes.
