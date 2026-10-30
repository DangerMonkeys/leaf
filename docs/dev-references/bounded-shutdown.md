---
title: Bounded shutdown
description: Shutdown deadlines, Bluetooth quiescence, recovery behavior, and developer fault testing.
---

Leaf must be able to release its battery power latch even if shutdown stalls on
storage, a driver, or Bluetooth cleanup. The guard bounds cleanup without adding
a task-manager slot. It does not depend on reproducing the original Goodbye freeze.

## Shutdown and recovery

The guard starts before the logbook mutex or any final save. Shutdown finalizes
the flight, closes the bus log, and saves settings before optional cleanup.
Bluetooth stops accepting work and parks its worker outside driver locks before
its queues, mutex, and task can be deleted. Full NimBLE deinitialization follows
power-latch release. Bluetooth cleanup initiated from settings/web also obtains
a guard; nested cleanup keeps the original deadline.

| Limit | Behavior |
| --- | --- |
| Eight seconds from shutdown request | A 100 Hz timer ISR releases GPIO48 if shutdown has not completed |
| Ten seconds from shutdown request | An independent RTC watchdog resets the processor if USB/button power keeps it alive |
| Ten-second continuous center hold | The ISR releases GPIO48 even if the main loop is blocked; release the button to remove its physical power source |
| 500 ms | Maximum wait for Bluetooth producer draining and worker parking |
| One second | Shutdown sound polling budget, within the overall deadline |

The normal 2.5-second Goodbye pause remains inside the shutdown budget. Eight
seconds bounds latch release when the timer interrupt can run; it is not an
unconditional bound with interrupts unavailable. Existing task/interrupt
watchdogs remain enabled and may act before the RTC backstop.

During an armed shutdown the emergency ISR **preserves the original RTC timer**:
it does not feed, disable, or shorten it. Earlier native tests failed after
changing an armed timer to a shorter reset timeout, while the original backstop
recovered from the same late shutdown state. The precise failure mechanism is
unproven. A shared policy regression asserts zero RTC register writes at the
eight-second deadline and expiry at the original ten-second bound.

An emergency without a fully armed guard, including an out-of-shutdown long hold,
needs a new one-second reset timer. Initialization/completion races retain that
fallback. A second one-second RTC-domain reset stage backs up the system reset.
Flash-boot watchdog protection is explicitly cleared when disarming the guard.

A 12-byte RTC no-init record retains stage and reason. A warm reset with valid
intent enters charging with the latch low, overriding a held button or persisted
boot-to-On request. Recovery initialization is itself guarded. The button must
be released before a fresh hold counts. Cold, brownout, and deep-sleep boots do
not interpret the record as a shutdown recovery.

## Resources and diagnostics

There is no new FreeRTOS task, stack, or task-manager slot. One GPTimer is added
(three of the ESP32-S3's four are used), plus fixed state and SDK timer allocation.
The RTC watchdog is separate from the task/interrupt watchdogs. Control calls and
the timer ISR share Arduino's loop core; ISR state uses aligned volatile words
rather than this toolchain's locking atomic read-modify-write helpers.

The emergency path uses internal/RTC memory and direct GPIO/register operations,
with no mutex, allocation, logging, driver, or software restart handler. Successful
recovery prints its stage/reason and appends a `shutdown,recovered` system event
when Developer/System Events logging and firmware SD ownership permit it. No
storage write occurs in the ISR. Power loss can erase retained intent, and a
storage failure can prevent both the last save and a diagnostic record.

## Developer fault testing

Normal firmware compiles out the fault selector and injected stalls. The explicit
`leaf_3_2_6_shutdown` build enables `LEAF_SHUTDOWN_FAULT_INJECTION`; Developer mode
must also be on. In **Menu > Developer > Off fault**, RIGHT/CENTER cycles choices
and LEFT returns to Back. Choices are nonpersistent and one-shot, defaulting to
OFF. Keep this build for assessment.

| Choice | Injection point or behavior |
| --- | --- |
| OFF | Normal shutdown |
| LOCK | Before the logbook mutex |
| SAVE | Before flight finalization |
| WORK | Before Bluetooth worker quiescence |
| BEEP | Before shutdown sound |
| I2C | Before peripheral sleep |
| SLP | After peripheral sleep, before display clear |
| CLR | After display clear, before latch release |
| LAT | After latch release, before entering charging state |
| BLE | Before full NimBLE deinitialization, after entering charging state |
| WDT | Interrupts disabled before flight save; the interrupt watchdog may act first |
| RTC | Guard ISR skipped before flight save; scheduler/interrupts remain active |
| RTCB | Guard ISR skipped at the late BLE boundary |

BLE fault tests emergency recovery before actual NimBLE deinitialization; normal
OFF is also required to test teardown. LOCK/SAVE can interrupt the final save.
Post-save faults should retain a complete summary and any successfully closed
track. On battery, power may disappear before late fault points execute.

Run the simulator build and regression suites with populated `.pio/libdeps`:

```sh
make -C sim -j8 all test-shutdown test-shutdown-menu test-developer-logging test-runtime-capture test-runtime-lifecycle
```

The simulator checks fresh save fixtures, deadlines/wrap, hold/release gating,
producer draining, nested guards, fault boundaries, recovery boot, and menu
layouts. Its RTC policy model is shared with native code; it does not model ROM
boot, retained peripheral state, actual radios, or physical power removal.

For hardware assessment:

1. Begin with OFF and an active flight. Test Bluetooth off, advertising, and a
   connected client. Verify normal charging, teardown checkpoints, and saved
   summary/IGC. Enable track saving and obtain synchronized GPS time for IGC tests.
2. Exercise faults with USB attached, releasing center after about three seconds
   and allowing twenty seconds for charging. Repeat holding center through reset,
   then release and verify a fresh power-on. Leave a stuck device attached before
   power cycling so USB inventory can be captured.
3. Exercise a pre-latch fault such as CLR on battery, then verify a fresh startup
   and stable On after button release. Use GPIO48/current instrumentation for
   absolute cutoff timing; a blank LCD alone does not prove power loss.
4. Preserve system events, flight files, firmware version/settings, and any core
   dump with the exact BIN/ELF. Validate representative long/slow-SD saves and
   burdened runtime overhead before release. See [runtime assessment](runtime-assessment.md).

Testing a healthy power-button shutdown does not establish the independent
long-hold path: that native check needs the ordinary main loop actually blocked
outside an armed shutdown. Recovery initialization can itself fail and reset
repeatedly while USB/button power remains; this is not protection against every
processor or hardware fault.

Current evidence and remaining coverage are in the
[shutdown validation report](bounded-shutdown-assessment-2026-10-29.md).
