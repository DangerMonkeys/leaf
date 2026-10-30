# Bounded shutdown assessment

This branch starts at main (`07140964`) and does not contain the route optimizer.
It addresses shutdown robustness independently of the unresolved cause of the
reported Goodbye freeze. It is a test candidate, not a firmware release.

## Behavior

- `Power::shutdown()` arms an eight-second deadline before acquiring the logbook
  mutex, writing files, or touching a peripheral. Nested Bluetooth cleanup keeps
  that original deadline. Normal shutdown disarms it only after reaching charging.
- Flight finalization, bus-log close, and settings save precede optional teardown.
  The sound polling loop has a one-second budget; the outer deadline also covers
  an individual sound/driver call that blocks. The existing 2.5-second Goodbye
  pause remains inside the overall budget.
- Bluetooth first stops accepting work, drains in-flight producers, wakes its
  worker, and waits at most 500 ms for it to park outside driver locks. Only then
  may queues, its mutex, and its task be deleted. It is unsafe to delete a task
  while it owns a NimBLE/advertising lock. Full NimBLE deinitialization is deferred
  until after releasing the power latch. Bluetooth cleanup from settings/web
  also obtains a bounded guard; a failed cleanup powers down rather than freeing
  resources still in use.
- An independent 100 Hz hardware-timer interrupt watches GPIO2. Holding the center
  button for ten seconds releases GPIO48 even when the main loop is blocked.
  Release the button afterward: a physically held button itself supplies power.
- The emergency ISR directly releases GPIO48 and arms a roughly 20 ms RTC watchdog
  reset if USB/button power keeps the CPU alive. Its critical path uses no driver,
  mutex, allocation, logging, or software restart handlers. Control calls are
  confined to Arduino's loop core, which also installs the timer. ISR state uses
  aligned volatile words rather than this Xtensa toolchain's locking atomic RMWs.
- A separate ten-second RTC watchdog backs up the shutdown deadline if the timer
  interrupt cannot run. Existing task/interrupt watchdogs remain enabled and may
  act sooner. The RTC slow-clock frequency is read from the SDK; physical timing
  and latch behavior still require hardware measurement. Eight seconds is the
  normal interrupt-driven limit, not an unconditional bound with interrupts or
  flash cache unavailable.
- A 12-byte RTC no-init record stores shutdown intent, last stage, and reason.
  A retained intent on a reset overrides both a held center button and a persisted
  OTA boot-to-on request, entering charging with the latch low. Recovery boot is
  itself bounded. A held button must be released before a fresh hold can count.
  The record is cleared on successful completion. Cold/brownout/deep-sleep boots
  do not interpret it as a shutdown recovery.
- Recovery prints `Recovered shutdown: stage=... reason=...` to serial. With
  Developer mode and System Events logging enabled, it also appends a
  `shutdown,recovered` event to `/diagnostics/system_events.csv` when the card is
  writable by firmware. No SD/NVS operation occurs in the emergency ISR. Battery
  power loss can erase the RTC record, and a storage failure can prevent saving
  either the last flight or a recovery event.

## Resource and scheduling impact

No FreeRTOS task or stack and no task-manager slot are added. One additional
GPTimer is allocated at boot (three of the ESP32-S3's four timers are now used),
plus small fixed control state and the SDK timer/interrupt allocation. The RTC
watchdog is distinct from the existing task/interrupt watchdogs. Bluetooth gains
an admission counter/stop flag and a worker acknowledgement. CPU/heap overhead
needs a bounded on-device runtime capture; desktop timings are not a substitute.
A future hardware variant, SDK change, or user of the RTC watchdog must revisit
these assumptions. The simulator uses an extra fake timer for the RTC backstop.

## Desktop checks

Run the formatter and the normal simulator build, then:

```sh
make -C sim -j8 test-shutdown test-developer-logging test-runtime-capture test-runtime-lifecycle
```

`test-shutdown` checks deadline/button counter wrap, held-button release gating,
concurrent producer draining, normal shutdown and saved flight, faults disabled
outside Developer mode, injected stalls before the logbook lock and at flight
save/worker/sound/peripheral/NimBLE stages, a completely unserviced main loop,
nested deadlines, an independent RTC-backstop model, and recovery boot with the
center button held. Hardware radios, I2C, actual SD latency, and the native RTC
watchdog are not emulated. Injecting a stall *at* a stage tests the outer guard;
it does not establish that the real driver failure has been reproduced.

## Hardware candidate and checklist

The ordinary `leaf_3_2_6_release` build includes the protections but no fault menu.
`leaf_3_2_6_shutdown` additionally defines `LEAF_SHUTDOWN_FAULT_INJECTION=1`.
Its Developer menu has a nonpersistent, one-shot **Off fault** selector, default
**OFF**. Developer mode must also be enabled. Test faults consume their selection;
reboot clears it. Use this build only for assessment.

| Selector | Deliberately stalls at | Expected observation |
| --- | --- | --- |
| OFF | None | Normal shutdown, flight saved, charging on USB, off on battery |
| LOCK | Before logbook mutex | Latch release/reset near eight seconds; final save may not occur |
| SAVE | Before flight finalization | Same bound; flight may remain incomplete |
| WORK | Before Bluetooth worker stop | Same bound; essential flight save already completed |
| BEEP | Before sound/peripheral sleep | Same bound; essential save completed |
| I2C | Before peripheral sleep | Same bound; essential save completed |
| BLE | Before full NimBLE deinit, after latch release | Battery latch is already low; USB resets near eight seconds |
| WDT | Interrupts disabled before flight save | Existing interrupt watchdog may act first; RTC backstop near ten seconds if needed |

1. First use **OFF**, USB connected, Bluetooth off: start a short flight/timer,
   then shut down. Confirm the charging screen and a complete flight/IGC.
2. Repeat with Bluetooth enabled, both advertising and with a connected client;
   include several power-on/off cycles and Bluetooth off/on toggles. Check that
   advertising and NUS data still work after reinitialization. Repeat with FANET
   enabled. Capture a short runtime report before and after if assessing overhead.
   Include a representative long flight and a slow/nearly full SD card before
   accepting the eight-second save budget for production.
3. Repeat normal shutdown on battery. Confirm that it actually loses power after
   releasing center, rather than merely blanking the display.
4. With USB attached, exercise each injected fault once. Time from the shutdown
   request until reset/charging, record the selector, Bluetooth/FANET/client state,
   and serial recovery line. Copy `/diagnostics/system_events.csv`, boot/heap
   diagnostics, any watchdog/core dump, and the test flight's logbook/IGC. Save
   these files before another build, and report whether USB storage returned.
5. For LOCK/SAVE, incomplete last-flight data is an expected tradeoff of forcing
   power down. For WORK/BEEP/I2C/BLE, verify the preceding flight finalized.
6. While an injected stall is in progress, continue holding center through reset;
   charging must remain selected. Release it, then power on normally. Separately
   test a ten-second hold during ordinary operation: the latch must release, and
   releasing center must let battery power disappear. This emergency hold can
   interrupt the current flight save.
7. Test selected faults on battery, including BLE. Because battery power can go
   away before the test-stage stall executes, BLE may simply power off normally.
   Confirm physical power loss. A logic analyzer on GPIO48 is preferable when
   validating absolute timing; visible LCD/USB changes include reset/boot delay.

Do not turn these results into a claim of protection against every processor,
power-circuit, or hardware fault. If recovery initialization itself cannot finish,
its watchdog may retry until USB/button power is removed. Physical power-removal
hardware remains the final remedy for failures beyond working GPIO/reset logic.
