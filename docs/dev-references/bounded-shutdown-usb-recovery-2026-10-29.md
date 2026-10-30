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

## Next diagnostic candidate

The next candidate adds fault boundaries without changing watchdog configuration,
reset code, shutdown order, deadlines, or task scheduling. Existing selector order
and stage IDs are preserved; new stage IDs are appended. Added selectors:

| Selector | Stall location | Reset path |
| --- | --- | --- |
| SLEEP | After peripheral sleep, before display clear | Eight-second emergency deadline |
| CLEAR | After display clear, before latch release | Eight-second emergency deadline |
| LATCH | After latch release, before `OffUSB` | Eight-second emergency deadline |
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
