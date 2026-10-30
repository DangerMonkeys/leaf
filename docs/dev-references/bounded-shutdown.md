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
- The emergency ISR directly releases GPIO48. During an active shutdown it
  preserves the already-armed ten-second RTC backstop: no watchdog register write,
  feed, or timeout change occurs at the eight-second deadline. If USB/button power
  keeps the CPU alive, the original watchdog resets it near ten seconds after the
  shutdown request, followed by a one-second RTC-domain escalation if needed.
  An emergency outside a fully armed shutdown (including a ten-second button hold)
  arms a one-second reset timer instead. Flash-boot protection remains armed
  across reset and is explicitly cleared when the application disarms the guard.
  The latch releases immediately; only the later processor restart is delayed.
  Its critical path uses no driver,
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
make -C sim -j8 test-shutdown test-developer-logging test-runtime-capture test-runtime-lifecycle test-shutdown-menu
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
reboot clears it. RIGHT/CENTER cycles the selector; LEFT returns to Back, as in the other Developer settings. Use this build only for assessment.

| Selector | Deliberately stalls at | Expected observation |
| --- | --- | --- |
| OFF | None | Normal shutdown, flight saved, charging on USB, off on battery |
| LOCK | Before logbook mutex | Latch release near eight seconds; RTC reset near ten; final save may not occur |
| SAVE | Before flight finalization | Same bound; flight may remain incomplete |
| WORK | Before Bluetooth worker stop | Same bound; essential flight save already completed |
| BEEP | Before sound/peripheral sleep | Same bound; essential save completed |
| I2C | Before peripheral sleep | Same bound; essential save completed |
| BLE | Before full NimBLE deinit, after latch release | Battery latch is already low; RTC reset near ten seconds |
| WDT | Interrupts disabled before flight save | Existing interrupt watchdog may act first; RTC backstop near ten seconds if needed |
| RTC | Guard ISR deliberately skipped; interrupts and scheduler remain running | Exercises the independent ten-second RTC backstop, without the normal eight-second guard or interrupt-watchdog panic taking over |
| SLP | After peripheral sleep, before display clear | Isolates sleeping peripherals from subsequent display/latch/state changes |
| CLR | After display clear, before latch release | Isolates display clear from latch release |
| LAT | After latch release, before entering `OffUSB` | Isolates latch release from the state assignment |
| RTCB | Same late boundary as BLE, guard ISR skipped | Compares independent RTC recovery with the BLE emergency deadline after peripheral sleep |

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
   and serial recovery line. The eight-second limit measures latch release, not
   the subsequent USB disconnect/reconnect: the preserved RTC deadline adds about
   two seconds to the original backstop, possible escalation another second, and initialization further time.
   Copy `/diagnostics/system_events.csv`, boot/heap
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

## Hardware follow-up

The first `cc457326` candidate saved the WORK test flight correctly but failed
BLE-fault recovery on USB: the LCD cleared, the LED went off at the reset, and no
USB interface returned until power was cycled. Initial eight-second/long-hold
simulator success did not establish that a native RTC reset could reboot.
The separate WDT fault did recover through the existing interrupt-watchdog panic
path; system events confirm two successful recoveries of that type.

The emergency path was also shortening an already-running watchdog's timeout
without first disabling it. Its old elapsed count could exceed the new timeout
before the subsequent feed reached the RTC domain. Both ordinary enable and
flash-boot mode are now disabled before configuration, then the watchdog is fed
and enabled after both stages are ready. A shared register-operation policy is
exercised by a host timing model that starts with eight seconds already elapsed;
it detects premature reset if either enable mode remains active during a timeout
change. This tests configuration order, not physical ROM reboot behavior.

The follow-up replaces the 20 ms single reset stage with the pinned SDK's
one-second stage timing, a second RTC-domain reset stage, and flash-boot
protection. It also clears that protection on disarm (ordinary WDT disable does
not necessarily disable flash-boot protection). The initial ten-second backstop
has the same second reset stage. Compare the pinned ESP-IDF
[restart implementation](https://github.com/espressif/esp-idf/blob/083aad99cf/components/esp_system/port/soc/esp32s3/system_internal.c).
These are corrections to a weak reset configuration, not proof that every
possible cause of the observed hardware failure has been identified.

Repeat BLE and RTC tests with USB connected, both with center released shortly
after shutdown starts and held through recovery. Confirm a return to charging,
then release and confirm a later ordinary power-on works. Until those trials
pass, USB reset recovery remains an open hardware issue. The simulator does not
model boot ROM timing, retained peripheral state, or RTC-domain reset behavior.


Candidate `53927919` physically recovered from RTC, LOCK, and I2C faults on USB,
with corresponding completed recovery records on the SD card. BLE still stalled
with a blank LCD, LED on, and no USB interface. The additional SLP/CLR/LAT
and RTCB selectors isolate the intervening operations; they do not change the
reset mechanism. See [the detailed USB recovery assessment](bounded-shutdown-usb-recovery-2026-10-29.md).


Later differential tests on `e93daf78` confirmed SLP recovery, a stuck CLR trial,
and successful RTCB recovery from the late BLE boundary. During active shutdown,
the deadline ISR now leaves the original backstop untouched rather than shortening
it to a one-second timer. The shared policy model asserts zero register writes and
expiry at the original ten-second bound. A new timer is still armed for emergencies
without an active guard, and initialization/completion races retain their fallback.
The exact cause of the prior native reset failure remains unproven; this change
uses the independently successful path and requires CLR/BLE hardware retesting.
