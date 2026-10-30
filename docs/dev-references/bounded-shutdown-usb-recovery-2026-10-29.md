# Shutdown USB-recovery follow-up — 2026-10-29

**Hardware acceptance remains open.** This records a failure in candidate
`cc457326` and the replacement test candidate `5392791926172105271d8c0eb85b35a88fedd02b`
(`0.2.4-539.shutdown+h3.2.6`) on `codex/bounded-shutdown`. No nav-optimizer
changes are included. The attached device is 3.2.6, identity `F0F5BD5467A0`.

## Observations and preserved evidence

The maintainer reported that BLE fault blanked the LCD around five to six seconds
and reset/disconnected USB near ten seconds, but did not return to charging.
Holding center through reset did not help; unplug/replug recovered it. WDT fault
returned to charging, both held and released. WORK fault saved an active short
flight and showed Goodbye, with no shutdown sound.

A repeat BLE fault was observed while the device was left attached and stuck:
Windows reported no Leaf COM port, no present VID 303A USB interface, no USB error
device, and no Leaf SD volume. This is consistent with failure before normal USB
startup; it does not identify the exact ROM/application instruction where boot
stopped. No new completed RTC/BLE recovery is recorded in the SD log.

Before any firmware changes, 129 SD files totaling 12,455,138 bytes were copied
and verified by SHA-256. The 64 KiB coredump partition was subsequently preserved;
its stored record has a valid CRC32, but its originating firmware/backtrace has
not been established. It must not be attributed to this failure without decoding.
The copied `fatal_error_1.txt` names older firmware `0.2.3`, not this candidate.

Raw evidence directory (ignored, local only):
`sim/build/hardware-evidence/shutdown-followup-20261029-183249/`.
It includes the SD snapshot/manifest, stuck USB inventory, raw coredump and read
logs, and checksum/notes inspection. Private flight files are not committed.

### Saved WORK flight

- JSON: `2026-10-29_18-30-28_6acd863b.json`, schema v0.1.0, `complete: true`,
  revision 3, duration **11 seconds**, firmware `0.2.4-cc4.shutdown+h3.2.6`.
- IGC: `2026-10-29-XLV-000-05.igc`, 11 B records from 01:30:29Z through
  01:30:39Z, final `LXLVEND ... CAUSE=POWER_OFF` record and G checksum.
- The repository's `vali-xlv.exe` returned **STATUS_IGC_PASSED**, exit 0.
- JSON SHA-256: `7ecce4a52f1c9ac828be31c76d55a028e014f4d9adbd1f4324feeffccc441efd`.
- IGC SHA-256: `5bed65852df7f15b5766d5c875d98fd96a5ffe67469b8449723249169824b810`.

No sound with WORK is intentional: the fault executes after final save and
Goodbye but before playback. The file evidence supports essential-save ordering.

### Reset diagnostic comparison

The system-events tail contains two completed recoveries with reset reason
`interrupt_watchdog` (5), followed by
`shutdown,recovered,flight-save,reason,4`. These correspond to the WDT fault path,
not proof that our direct RTC reset worked. An intervening interrupt-watchdog
boot has no completed setup/recovery row, so not every attempted boot is assumed
successful.

The CSV has a malformed prefix and 1,488 non-ASCII/NUL bytes. Its entire first
1,733,292 bytes exactly match the earlier nav A/B snapshot, proving that damage
predates the shutdown test build. Tail records were analyzed independently of the
missing/damaged header. No repair or deletion was performed on the SD card.

## Corrections in replacement candidate

The previous emergency handler shortened an already-running ten-second RTC
watchdog to roughly 20 ms without stopping it. At the normal deadline, roughly
eight seconds were already counted. That is an unsafe update: the old count can
exceed the new limit before the later feed reaches the RTC domain. This is a
concrete configuration risk, not proof that it alone explains the physical boot
failure.

Native arming and emergency reconfiguration now share a forced-inline policy:

1. Unlock writes; clear flash-boot mode and ordinary enable.
2. Configure SYSTEM reset and a second RTC-domain reset stage.
3. Feed the counter before re-enabling either mode, then protect writes.

Disarm clears both enable modes. The emergency reset uses one-second stages,
matching the timing/escalation shape of the
[pinned SDK restart implementation](https://github.com/espressif/esp-idf/blob/083aad99cf/components/esp_system/port/soc/esp32s3/system_internal.c).
The initial independent backstop remains ten seconds, with the same one-second
RTC-domain escalation. The eight-second latch-release deadline is unchanged.
The emergency path still uses direct registers, with no driver, heap, mutex, or
software shutdown handlers.

A new **RTC** selector skips only the guard ISR's logic and leaves interrupts and
the scheduler running. This exercises the independent hardware RTC backstop.
The existing **WDT** selector still disables interrupts and may be handled first
by the existing interrupt watchdog. Neither fault saves the current flight before
stalling, because both are injected at the flight-save stage.

## Verification and deployment

- Formatter and `git diff --check` passed; both normal and fault-test 3.2.6
  PlatformIO builds succeeded from the clean replacement-candidate tree.
- Full simulator build, shutdown policy/integration tests, developer menu,
  developer logging, runtime accounting, and runtime lifecycle checks passed.
- A register timing model exercises the exact configuration-order helper used by
  native code. It detects premature expiry when a live timeout is shortened or
  only one enable mode is stopped; checks safe rearm with eight seconds already
  elapsed, first SYSTEM reset, subsequent RTC reset, and full disarm.
- Thirteen integration scenarios pass, including separate interrupt-off and
  RTC-only fault modes. The simulator does not model the physical boot ROM or
  reset-domain retention and cannot establish that USB recovery is fixed.
- Both ELFs keep emergency functions and their dependencies in internal/RTC
  memory. Native register adapter operations are forced inline. Static RAM in
  the developer build is 107,420 B; no task-manager slots/tasks were added.
- Both OTA app slots were flashed and verified, exit 0. Application USB returned
  on COM14/PID 0002 and SD access returned. Both test route hashes are unchanged.

Replacement fault-test BIN: 2,417,440 bytes, SHA-256
`7af82322c938eef24f4a1b8db3840f987c55a54f29af409128c46a72dc1f2194`.

Replacement build/deployment evidence:
`sim/build/hardware-evidence/shutdown-recovery-53927919-20261029-185835/`.
The exact BIN/ELF pairs, build IDs, test/build logs, IRAM audit, and deployment
records are retained there. Successful post-flash boot is not an injected-reset
recovery test.

## Replacement-candidate physical results

Candidate `53927919` still fails the BLE fault with USB connected. The maintainer
heard the normal shutdown sound, saw Goodbye and then a blank LCD, but the LED
stayed on. While left attached, Windows again reported no Leaf USB interface or
SD volume. This does not establish whether the emergency ISR ran or where a
subsequent boot stopped; LED state alone is insufficient to distinguish them.

After unplug/replug, **RTC fault returned to charging**. The preserved CSV tail
records `boot,reset,"other_watchdog",reason,7`, completes peripheral and task-manager
initialization, reaches `setup-complete`, and then records
`shutdown,recovered,flight-save,reason,4` at 2,105 ms. This is physical evidence
that the independent RTC watchdog can reset and complete recovery from the early
flight-save fault stage on this exact candidate. It does not prove recovery from
the later BLE-deinit stage or from the timer ISR's emergency reconfiguration.

The differential evidence is retained locally in
`sim/build/hardware-evidence/shutdown-second-failure-20261029-190811/`, including
`stuck-state.json`, `system-events-after-rtc.csv`, and `rtc-result.json`. The CSV
copy was verified against the SD source by SHA-256:
`c6803726ab8dfdc5f00442420fd9a3b5859980466f9acb1338dc609e57b034cc`.

**LOCK also returned to charging**, approximately twelve seconds after the initial
button press. The CSV records reset reason `other_watchdog` (7), completed setup,
and `shutdown,recovered,logbook-lock,reason,1` at 2,103 ms. Thus the timer ISR's
emergency deadline path can recover from this early stage. Its CSV copy has
SHA-256 `aee9403dc74e002a72686b10075943d13023d3b84849ed842dfc0660f6e99600`.

**I2C also returned to charging**, after sound and Goodbye and about ten seconds
on the splash screen. Its CSV records reset reason `other_watchdog` (7), completed
setup, and `shutdown,recovered,peripherals,reason,1` at 2,106 ms. Its CSV copy has
SHA-256 `48d8082a8b2e2a6f1cc7a0b0dfc0db0d4d324688220e2f2c494b29d43f1f8f12`.
These verified copies and maintainer observations are in the same evidence folder.

These comparisons narrow the failure to state reached after the I2C fault point;
they do not establish which operation causes it. The remaining operations before
the BLE injection are peripheral sleep, display clear, latch release, and the
`OffUSB` state assignment. Actual NimBLE deinitialization has not run at the BLE
fault point and cannot yet be blamed for this injected failure.

## Deployed late-boundary diagnostic candidate

Candidate `e93daf78e766189f967b6b747a73a893e0951444`
(`0.2.4-e93.shutdown+h3.2.6`) adds fault boundaries without changing watchdog configuration,
reset code, shutdown order, deadlines, or task scheduling. Existing selector order
and stage IDs are preserved; new stage IDs are appended. Added selectors:

| Selector | Stall location | Reset path |
| --- | --- | --- |
| SLP | After peripheral sleep, before display clear | Eight-second emergency deadline |
| CLR | After display clear, before latch release | Eight-second emergency deadline |
| LAT | After latch release, before `OffUSB` | Eight-second emergency deadline |
| RTCB | Same late location as BLE, after `OffUSB` | Independent ten-second RTC backstop |

Each remains nonpersistent, one-shot, and restricted to the explicit developer
fault build. Test sequentially with USB connected, releasing center after about
three seconds and allowing twenty seconds for recovery. Stop at the first stuck
case and leave it attached for inventory capture. RTCB then distinguishes the
late hardware-backstop result from the late emergency-ISR result. The simulator
checks all boundaries, saved flights, selector cycling, and the power state at
the late stalls; it cannot reproduce physical reset retention.

Normal OFF/connected-client Bluetooth shutdown and representative long-flight SD
save latency remain production acceptance items. Hardware acceptance remains open.


### Verification and deployment of the diagnostic candidate

- Formatter and whitespace checks passed. Both normal and developer-fault 3.2.6
  firmware builds passed with clean-source identifiers for `e93daf78`.
- All 17 shutdown integration scenarios, shared RTC policy checks, concurrent
  producer-drain checks, runtime accounting/lifecycle, and developer logging/menu
  tests passed. Twelve injected stalls respect their modeled bounds. Rendered
  SLP and RTCB menu screenshots were checked for clipping; short labels fit.
- The emergency functions remain in IRAM, with no mutex/atomic helper or flash
  constant references in their audited relocations. Static RAM remains 107,420 B
  in the developer build; no task, stack, or task-manager slot was added.
- The exact BIN/ELF pairs, build identifiers, test/build logs, IRAM audit, previous
  hardware result records, flash logs, and post-flash checks are preserved in
  `sim/build/hardware-evidence/shutdown-recovery-e93daf78-20261029-192644/`.
- Both OTA app slots were flashed and hash-verified, exit 0. The app-mode attempt
  caused re-enumeration; the single retry matched the same Leaf identity in
  bootloader mode. Application USB returned on COM14/PID 0002. Both testing route
  hashes on the SD card are unchanged.
- Developer BIN: 2,417,616 B, SHA-256
  `c5b0e8a7ca358ce7f3315971f6120617ce7a8016346d9e6567ca6598d889846b`.

Successful post-flash startup is not proof of recovery from these injected late
faults. The maintainer has been asked to test SLP first, with USB attached, center
released after about three seconds, and a twenty-second observation window.


### SLP physical recovery confirmed

On the deployed `e93daf78` candidate, the maintainer observed proper shutdown
sound and Goodbye, approximately ten seconds displaying the splash, and a
successful reboot to charging. The copied CSV records reset reason
`other_watchdog` (7), completed setup at 2,083 ms, then
`shutdown,recovered,peripherals-asleep,reason,1` at 2,104 ms. COM14/PID 0002 and SD
access returned. Thus recovery succeeds after `sleepPeripherals()` and before
`display.clear()` on this trial; peripheral sleep alone does not reproduce the
late BLE-boundary failure.

The exact-candidate evidence directory above now includes `slp-result.json` and
`system-events-after-slp.csv`; the CSV copy was verified against the SD source.
SHA-256: `8cf5a23e973b074b6f7334ff689480409934ea109d2b6074ff197a3f3e72a58b`.
The next requested comparison is CLR (after display clear, before latch release).
This single successful trial does not establish repeatability or close hardware
acceptance. No additional firmware was flashed for this test.


### CLR failure and RTCB success on the same diagnostic candidate

CLR cleared the display and then remained stuck. Windows reported no Leaf serial
port, present VID 303A USB interface, USB error device, or accessible SD volume.
Inventory is saved as `clr-stuck-state.json`, `clr-stuck-usb.json`, and
`clr-stuck-volumes.json` in the `e93daf78` evidence folder. The observation bounds
the failing transition after SLP and before normal latch release/OffUSB. It does
not prove the clear call returned or locate the last CPU instruction.

Unplug/replug restored charging. The preserved `system-events-after-clr-power-cycle.csv`
has no completed CLR recovery before that power-on boot. SHA-256:
`f4b999c0f8fbd6f88707967d866a96c9fafac6c69f8e9b305a4a089338b27c3c`.

RTCB then produced normal sound/Goodbye, cleared the screen at the usual time,
and rebooted to charging about twelve seconds after the initial press. The log
records `other_watchdog` (7), completed setup at 2,086 ms, and
`shutdown,recovered,ble-deinit,reason,4` at 2,106 ms. Thus the independent hardware
backstop can recover from the late state. `system-events-after-rtcb.csv` has
SHA-256 `c4d205e505f8c4ddc101f42cb4b4b8dc1e70ec77f5afb9a425e563aa4f117049`;
it and `rtcb-result.json` are preserved alongside the CLR evidence.

### Preserve the original backstop during an active shutdown

The next correction leaves the original ten-second watchdog entirely untouched
when an already-armed shutdown reaches its eight-second emergency deadline. The
ISR still releases the latch immediately and records the emergency reason. USB
recovery then follows the same original timer used by the successful RTCB trial,
rather than disabling, feeding, and shortening it from the timer ISR. No driver,
mutex, extra task/slot, heap allocation, or software restart handler is introduced.

The shared native/test policy explicitly asserts zero watchdog register writes
for an already-armed emergency, and reset at the original ten-second deadline.
An out-of-shutdown long hold or interrupted guard initialization/completion still
arms a reset watchdog; those fallbacks have not been removed. Simulator latch
checks still measure the eight-second release; physical reboot occurs later.

This is an evidence-based simplification, not proof of why a one-second rearm
failed after display clearing. Native CLR and BLE must be retested, followed by
normal shutdown, long hold, and the outstanding production acceptance checks.


### Preserve-backstop candidate deployed; CLR recovery confirmed

Source `bebbbf8faf4e5de082ae31d728aa65d13b5ea36c`, firmware
`0.2.4-beb.shutdown+h3.2.6`, was built from clean source in both normal and
fault-test 3.2.6 environments. Formatter/whitespace checks, all 17 simulator
shutdown scenarios, the shared watchdog policy regression, developer logging/menu,
and runtime capture/lifecycle checks passed. The emergency functions remain in
IRAM with the same audited constraints. Developer static RAM remains 107,420 B;
no task, stack, or schedule slot was added.

Both OTA slots were flashed and hash-verified, exit 0, with one app-mode nudge and
same-identity bootloader retry. Post-flash checks returned COM14/PID 0002, readable
SD storage, and unchanged hashes for both test routes. Initial read-only checks
overlapped the maintainer's CLR trial (storage/USB went away); subsequent checks
passed after its recovery.

The maintainer reports that **CLR now works and returns to charging**. Its log
records `other_watchdog` (7), `setup-complete` at 2,088 ms, and
`shutdown,recovered,power-latch,reason,1` at 2,107 ms. This verifies recovery through
the interrupt deadline path after display clearing on this trial. It supports
preserving the original hardware backstop; it does not identify the root cause
of the prior shortened-reset failure or establish repeatability yet.

Evidence is preserved in
`sim/build/hardware-evidence/shutdown-recovery-bebbbf8f-20261029-194137/`, including
exact BIN/ELF pairs, build/test/audit and deployment records, links to the previous
candidate's differential evidence, `clr-result.json`, and
`system-events-after-clr.csv` (verified against the SD source).
CSV SHA-256: `27fcd4e79414d2591cf15cead70b52053c8c0a7e9dca95b3592eda03b9f4d29e`.
Developer BIN: 2,417,632 B, SHA-256
`28cb311f131811bf16ece6b485455e42bd9e8ad68a7577b27c76f24a59ce2b50`.

The next requested physical check is the original BLE fault with USB attached,
center released after approximately three seconds, and twenty seconds allowed
for charging. Hardware acceptance remains open, including held-button/outside-
shutdown long-hold recovery and normal shutdown/save/radio regression checks.


### BLE-fault recovery with Bluetooth disabled confirmed

On `0.2.4-beb.shutdown+h3.2.6`, the maintainer reports BLE fault returned to
charging after about twelve seconds, with Bluetooth disabled in settings. The
CSV records `other_watchdog` (7), completed setup at 2,083 ms, and
`shutdown,recovered,ble-deinit,reason,1` at 2,103 ms. COM14/PID 0002 and SD access
returned. This confirms late-boundary emergency recovery, not active Bluetooth
cleanup: the injected stall occurs before full NimBLE deinitialization.

The current evidence folder includes `ble-disabled-result.json` and the verified
`system-events-after-ble-disabled.csv`, SHA-256
`2f3f921aa2c8b6b841706d64238cfab885e6182bd0f66d328154ebf981b445da`.
Next requested trial: enable Bluetooth broadcasting with no connected client and
repeat BLE fault with USB attached and center released after about three seconds.
Normal OFF with active Bluetooth must also be tested to exercise actual teardown.
No further firmware changes or flash were needed for this trial.


### BLE-fault recovery with Bluetooth broadcasting confirmed

The maintainer enabled Bluetooth broadcasting with no client connected and
reports successful restart into charging under BLE fault on the same `bebbbf8f`
candidate. The CSV shows Bluetooth setup/start completed before the test, then
`other_watchdog` (7), completed setup at 2,084 ms, and
`shutdown,recovered,ble-deinit,reason,1` at 2,103 ms. COM14/PID 0002 and SD access
returned. This covers the late guard after cooperative worker quiescence while
the Bluetooth stack had been active. The deliberate fault still precedes full
NimBLE deinitialization, so successful normal teardown is a separate check.

Evidence: `ble-advertising-result.json` and verified
`system-events-after-ble-advertising.csv` in the current candidate folder.
CSV SHA-256: `36d4508830c07d987c054a28ae8db4d71e4ba4653ba64958cc60ae4642beea3c`.
Next requested trial: Bluetooth enabled, Off fault OFF, at least ten seconds of an
active flight timer followed by normal shutdown, leaving the timer running until
shutdown. Verify normal charging transition, actual teardown checkpoints, and the
completed flight/IGC. Connected-client, held-button, long-hold, battery-only, and
representative slow/long-save checks remain open. No additional flash was needed.


### Normal advertising-enabled shutdown and summary save confirmed

With Bluetooth enabled, no client connected, Off fault OFF, and the flight timer
left running, the maintainer observed normal sound/Goodbye and a prompt return to
charging. The appended system-events interval has **no boot-reset or shutdown-
recovery record** and reaches all teardown checkpoints, including
`ble-end-nimble-after` and `ble-end-after`. Thus this trial completed actual normal
Bluetooth cleanup without watchdog recovery; it was not just a fault-stall test.

The new flight summary `date_not_available_91_fca5a743.json` names the corrected
firmware, is `complete: true`, revision 2, and has a **17-second** duration. It was
copied and hash-verified. The clock is unsynchronized and `track.saved` is false;
no new IGC exists. `Flight::startFlight()` intentionally waits for synchronized
GPS date/time before creating a track. This verifies summary finalization only;
IGC finalization on this candidate still needs a GPS-synchronized trial.

Preserved in the current candidate folder: `normal-advertising-result.json`,
`system-events-after-normal-advertising.csv`, its appended-events delta, and the
copied summary. CSV SHA-256:
`ed1ec768b2f7ac77f16d5fd5269ea0a9bd83dc62dcbd95fb5adc059434ec6930`.
Summary SHA-256: `47409c1633d480ead6bca8c6ee1fe3a7c6c1822f7d2c26029acc2d5eeced858f`.
An older undated record also copied during discovery names `0.2.1` and is not
attributed to this trial.

Next requested test holds center through BLE-fault recovery, then releases it,
waits two seconds, and checks an ordinary subsequent power-on. Battery-only,
connected-client, synchronized IGC, true blocked-loop long-hold, representative
save latency, and runtime-overhead acceptance checks remain open.
