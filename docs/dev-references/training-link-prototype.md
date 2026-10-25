---
title: Student and Instructor Link Prototype
description: Build targets, bench setup and limitations of the initial dedicated LoRa student/instructor firmware.
---

# Student and instructor link prototype

Initial implementation on `codex/instructor-student-link`, based on `89d938cf` from
`codex/lora-fanet-range`. This work uses an isolated worktree.

This is a bench prototype. A first two-device RF link and restart check have passed. Actual
phone-app compatibility, four-radio capacity, power behavior and flight/resource qualification
remain unverified. See the
[product backlog](backlog/student-instructor-training-link.md) for the complete requirements.

## Firmware targets

From the repository root:

```powershell
pio run -e leaf_student_3_2_5_training
pio run -e leaf_student_3_2_7_training
pio run -e leaf_instructor_3_2_6_training
pio run -e leaf_instructor_3_2_7_training
```

Use the local PlatformIO executable if `pio` is not on PATH. Images and separately named
`firmware-leaf_student_3_2_7.bin` / `firmware-leaf_instructor_3_2_7.bin` artifacts are under
`.pio/build/<environment>/`. The normal release defaults are unchanged. Student targets support v3.2.5 and v3.2.7; the
instructor targets support v3.2.6 and v3.2.7. Add/qualify other hardware revisions separately.
The training radio uses each variant's external RF-switch pin as well as the module's DIO2 switch.

- **Student:** the normal Leaf application plus one bus subscriber and a polled training
  service. Parsed GPS updates feed a fixed-size snapshot; pressure/altitude/climb come from
  the existing barometer instrument. Bus callbacks do no radio/SPI work. Settings > Leaf Labs >
  Training now controls runtime radio ownership: On reserves the radio for training; Off restores
  normal FANET lifecycle and preferences. A separate firmware name keeps the normal OTA asset name
  from being treated as this build's identity. A healthy student IMU/barometer is required.
- **Instructor:** a separate entry point, compiled from only the training protocol, radio
  and instructor sources. It initializes the normal board IO expander, but does not start local
  flight sensors, the estimator, local GPS, SD, the normal power manager, charging UI or task manager.
  A private typed message bus feeds four independent remote state records. Display and radio SPI access happen on one loop;
  the radio IRQ only sets a flag. Bluetooth callbacks do not access SPI. The instructor target
  disables only the display library's hardware-I2C backend with `U8X8_NO_HW_I2C`;
  the board-control expander still uses Wire on the normal I2C pins.

The instructor targets standard, unmodified boards. It holds the power latch, uses the existing
SPI display/direct button and speaker PWM pins, and initializes the IO expander using the variant's
normal output latches and pin directions. Speaker gain uses the same off/low/medium/high bit
settings as Leaf. It starts **enabled at medium volume**, or high if that was saved. A saved low
level or mute does not reduce startup below medium. Lower gain/mute remain available during use.
After startup it writes gain only when the requested level changes. No IMU, barometer or ambient-sensor addresses are probed,
so their absence or failure does not gate startup.

Board-control transactions use a 10 ms timeout. An expander communication fault is nonfatal,
is reported as unavailable volume, and disables further gain writes rather than retrying on the
RF/audio loop. PWM muting remains independent of expander access. Full battery protection,
charge-state reporting and shutdown/sleep still need implementation/qualification; the prototype
is not the normal Leaf power-management implementation.

## Climb-driven audio

The instructor generates vario audio from the selected student's valid climb rate, using the same
`ui/audio/vario_audio.h` pitch mapping, beep/rest envelope and pitch smoothing as production Leaf.
RF updates change the climb input at nominal 4 Hz; they do not restart the local beep clock. The
40 ms envelope and 10 ms pitch service continue between packets, including a missed packet.
Climb that becomes invalid or reaches the existing 750 ms age limit is silenced. Focus changes
reset the old voice. Instructor button clicks remain local sound effects and briefly take priority.

The instructor currently uses the **production default sound profile** (climb starts at +0.05 m/s,
sink alarm at -2.5 m/s). Audio profiles and thresholds belong to the instructor; student profiles
will not be transferred. A future instructor configuration UI should allow its own familiar profile.
Student quiet-mode settings do not control instructor audio. This reproduces Leaf's rhythmic synthesis for the received climb, without requiring
the student's volume to be enabled and without forwarding student button/UI sounds. The phases
of the two devices' beep clocks need not match.

Wire V1 remains compatible. Older student binaries may still send sampled PWM in the legacy
`toneHz` field; the instructor ignores it. New student builds leave it zero. The already-flashed
v3.2.5 student therefore works with the updated instructor without another student flash.

## Initial bench setup

Connect one device at a time, auto-detect its Espressif USB port (VID `303A`), close other serial
monitors, and flash the corresponding image to both existing Leaf OTA app slots (`0x10000`
and `0x340000`) using the normal repository hardware workflow. These offsets require the existing
Leaf 8 MB partition layout. Match each image to its hardware and role; the student needs working
flight sensors. Do not erase settings as part of this test. The student now uses **Settings > Leaf Labs > Training**:

- Enabled On/Off is saved independently of pairing. Off keeps enrollment and stops training RF;
  On reconnects to the saved instructor. A newly configured student defaults Off. A previously
  paired bench student's enrollment is migrated and its working enabled state retained.
- Pair requires Enabled On and opens a 60-second window. It broadcasts a short discovery Hello
  with bounded random jitter. An instructor Offer is shown by ID; select Confirm pair to approve.
  The student retries Accept until that instructor confirms the assigned group/slot. Only then is
  the binding saved. Timeout or Cancel preserves the previous pairing. Old polls never auto-enroll.
- Unpair opens a separate Keep pairing / Unpair confirmation. Removing the binding leaves training
  enabled but prevents telemetry until paired again. Returning to normal FANET still requires Off.
- The screen reports link state and instructor ID. Local instruments/audio/logging/Bluetooth are
  unchanged. Normal FANET remains subject to its existing region and flight/ground-tracking rules;
  Off does not start broadcasts when those rules would normally leave FANET idle.

The instructor now supports the matching discovery flow with its buttons:

1. Power on both devices. On the student, select Training Enabled On, then Pair.
2. Press Right on the instructor to open a 60-second Add student window. A first setup generates
   and saves a nonzero group automatically; existing groups/rosters are retained.
3. Select the student's full eight-digit ID with Up/Down, then press Right to offer the link.
4. Check the instructor ID shown on both screens, then select Confirm pair on the student.
5. The instructor saves the assigned roster entry before sending Confirm. Once validated telemetry
   arrives, it returns to the main roster and focuses that student for audio/Bluetooth output.

The first free slot is assigned automatically. A known student reuses its existing slot, including
when all four slots are occupied; a fifth new student cannot overwrite anyone. No group/slot USB
commands are needed. Existing paired units still reconnect without entering discovery. During
pairing, Left closes the instructor's pairing screen without removing saved links. If the final
confirmation/data is lost, Confirm is retried; a timeout after a local save retains the roster
entry as waiting/lost. Reopen Pair on both devices to retry an interrupted transaction; the same
identity reuses the saved slot. This is not cryptographic authentication.

USB diagnostics remain available at 115200 baud, with newline-terminated commands: student
`enable 0`, `enable 1`, `pair`, `accept`, `cancel`, `forget`, `status`. Group/slot USB setup is
replaced on the student by assignment through the new enrollment protocol. On the instructor,
diagnostic `pair`, `offer`, `cancel`, `status`, `focus <0..3>` and other controls remain available.
`group <number>` is a diagnostic reset of the entire roster, not part of field setup. Instructor
`group 0` disables RF until Add student is requested; `7101` is a historical bench group, not a default.

Instructor buttons on the main roster: Up/Down select focus, Left cycles mute/low/medium/high,
Right opens Add student, and holding center for three seconds forgets the focused student.
In discovery, Up/Down select a candidate, Right offers the link, and Left closes pairing. A center
hold started during pairing cannot remove a saved student. Display slot labels are 1-4; diagnostic
commands and wire slots remain 0-3. Button presses play the normal
Leaf click at the selected volume; mute also silences these local clicks. USB commands also support
`focus 0` through `focus 3`, `volume 0` through `volume 3` (off/low/medium/high), `mute`, `unmute`
and `forget`. `unmute` restores the remembered nonzero gain within the current session. Every
boot enables volume at medium minimum, preserving a saved high setting; mute is temporary.
Roster and selected focus persist. Group plus all four roster IDs are saved as one NVS blob;
legacy per-slot bench settings are read when that blob is absent. Student naming remains follow-up work.


The instructor advertises as `InstXXXX` (last four hex digits of its derived device ID), using
Leaf's Nordic UART service and RX/TX UUIDs. Pair the phone through its flying app and select the
external GPS/vario input. Confirm that the app uses the student's GPS rather than the phone's.
The only ownship stream is the selected student: GGA/RMC GPS sentences and LK8EX1 pressure/vario.
GGA currently reports unknown satellite count/HDOP; richer fix metadata needs adding before
app qualification. Unknown temperature/battery are sent as protocol sentinels, not instructor
sensor values. BLE sentences are checksummed and fragmented into 20-byte UART notifications,
so the initial ATT MTU does not truncate them.

## Polling and wire format

Provisional bench PHY: 920.8 MHz, 500 kHz bandwidth, SF7, coding rate 4/5, private LoRa sync word
`0x12`, explicit headers, two-byte radio CRC, 8-symbol preamble, 10 dBm requested power and
1.8 V TCXO. This is a matched bench configuration, not FANET and not a qualified production US
operating profile. Region/module/antenna requirements and installed RF performance remain open.

Range follow-up: the separate FANET branch `codex/lora-fanet-radio-settings` (`20d2ff29`)
requests 22 dBm and sets/verifies a 140 mA SX1262 current limit after initialization. The training
driver does not inherit that change: it still requests 10 dBm and retains RadioLib 7.1.2's
60 mA limit. Apply and verify both settings in the shared training driver for both roles before
range evaluation; the instructor-to-student poll is required before any student reply. Keep
the current PHY/schedule initially so power/current effects can be measured separately. FANET's
neighbor-dependent coding-rate policy does not apply to this private polling protocol. Requested
power/current settings are not measured RF output or a range guarantee.

An eight-slot cycle uses 125 ms slots. The focused student gets four polls; each of the three
others gets one, followed by an idle slot. Only the addressed student answers. No clock alignment
between devices or broadcast telemetry competition is required. These are nominal request rates;
round-trip latency and sustained real RF rates still need measurement. Instructor loss stops
student telemetry without affecting the student's local flight instruments. Discovery Hello/Accept
transmissions occur only during the explicit pairing window.

Protocol v1 uses explicit little-endian fields, never native object bytes:

| Offset | Field |
| --- | --- |
| 0-1 | Magic `54 4c` |
| 2-3 | Version 1, type (poll 1 / telemetry 2 / Hello 3 / Offer 4 / Accept 5 / Confirm 6) |
| 4-19 | Group, sender identity, boot session, request sequence; four uint32 values |
| 20 onward | Poll: requested slot and focused slot. Telemetry: fields below. |
| Last 2 bytes | CRC16-CCITT, polynomial `0x1021`, initial `0xffff` |

Polls are 24 bytes. Telemetry is 64 bytes, with slot/validity, GPS/pressure sample ages (uint16 ms),
legacy tone frequency (uint16 Hz, ignored for audio), pressure (uint32 Pa), signed latitude/longitude (1e-7 degrees),
signed GPS MSL and pressure altitudes (int32 cm), climb (int16 cm/s), speed (uint16 cm/s),
course (uint16 hundredths of a degree), UTC date (uint32 YYYYMMDD) and time (uint32 HHMMSScc).
The telemetry sequence echoes the poll and is accepted once, only for that pending slot/window.
Unknown versions, sizes, CRCs, groups, source identities and obsolete replies are rejected.

Enrollment control frames are 31 bytes: the same 20-byte header, target ID (uint32), target boot
session (uint32), assigned slot (uint8), then CRC16. Hello uses discovery group `0x54524e47`, zero
target/session/slot, and a fresh nonzero pairing-window nonce in header.sequence. Offer and Confirm
target the student's ID/boot session and echo that nonce; their header gives the instructor's
identity/boot session and assigned nonzero group. Accept reverses sender/target roles and echoes
the proposal's group/slot and nonce. Confirm must match the exact user-approved Offer. The instructor
retries confirmation after enrollment until it receives validated telemetry or its bounded handshake
expires, rather than allocating another slot on duplicate Accept. Offer/Confirm retries vary between
500 and 874 ms before slot scheduling, avoiding repeated collisions with the installed student's
fixed 500 ms Accept retry. Control frames use idle/unpaired slots or the pending student's slot;
other roster entries retain their polls. Enrollment IDs/nonces filter
accidental/stale traffic; they are not cryptographic authentication.

Focus changes cancel outstanding replies, clear partly queued BLE output and silence old tones.
Focused data expires after 750 ms; GPS validity additionally respects its original sample age
and a 2.5-second limit. Stale GPS emits an invalid fix and stale vario emits unavailable values,
following the [LK8EX1 specification](https://github.com/LK8000/LK8000/blob/master/Docs/LK8EX1.txt).
Actual phone behavior on invalidation, reconnect and source switching must still be tested.

## Two-device bench check, 2026-10-24

Source: `codex/instructor-student-link`, based on `89d938cf` with uncommitted prototype changes.
Both application slots were flashed and their contents verified. Group `7101`, student slot `0`,
and instructor focus `0` were used.

| Role | Hardware / device MAC | Firmware SHA256 |
| --- | --- | --- |
| Student | v3.2.5 / `f0:f5:bd:56:16:a0` | `add56d7998825e2643917b60aecb2be99399a8330ab1fe468aa68041f9991d3d` |
| Instructor | v3.2.7 / `48:ca:43:59:ae:a8` | `75da577af3a704d932eba7ed53ac0d85edfc24a33a3b21b1b9d5a65f57787742` |

- The student retained its group/slot after restart and reported a ready radio with zero errors.
- The instructor used its normal `0x20` expander and reported medium gain enabled with working
  volume control. No board-control address override or hardware bypass was used.
- A first pairing received 50 accepted telemetry packets. Steady five-second status intervals
  advanced by 20 packets, consistent with the nominal focused 4 Hz rate, with zero reported errors.
- After explicitly muting and restarting the instructor, it returned to medium volume and resumed
  receiving the saved student without another pairing command. Post-restart counts advanced from
  19 to 39 accepted packets while RX/TX counts matched, again with zero reported errors.
- The student had no GPS fix. This verifies the telemetry link without a fix, but does not validate
  mapped position, phone-app input, audible tone fidelity, range or four-student capacity.

The instructor's Bluetooth name is `Inst62E6`; actual XCTrack/SeeYou Navigator testing remains
outstanding. Local flash/runtime captures are retained under `.pio/training-*-3-2-*-*.log` and the
corresponding `*-device.json` records; `.pio` is ignored and is not a published evidence archive.

## USB stall fix and audible checks, 2026-10-24

The initial instructor used a zero HWCDC TX timeout. In the bundled Arduino driver, a stalled
write can decrement an unsigned zero retry counter, leaving the main loop waiting when the host
stops reading. This could freeze display/buttons/RF/audio and then recover when a monitor opened.
The instructor now uses a nonzero 1 ms timeout and drops periodic diagnostic lines when the TX
buffer lacks space. SPI scheduling was not changed for this fix.

Updated instructor firmware SHA256: `8d54929354769a1d8a0e96e0462e739a506b108f03777930b29cde9717d18505`. Both application slots were
flashed and verified. With DTR/RTS deasserted for the regression capture, the serial monitor was
closed for 22 seconds; accepted telemetry advanced from 212 before closure to 352 after reopening,
with zero reported radio errors. Five-second status intervals continued advancing by 20 frames.
The operator confirmed button clicks, live display updates and remote vario audio.

For a quick bench check:

1. Power on both units. The student may be muted: the instructor now uses climb values, not
   its speaker output. Enable instructor volume.
2. On the instructor, confirm `RX` advances. Up/down move the focus marker; left cycles volume.
   Button clicks test its speaker independently of student telemetry. Return to paired slot `0`.
3. Lift/lower the student and compare climb values and beeps. Moving the instructor has no effect
   because its local flight sensors are unused. A GPS fix is not required for this check.
4. Turn off the student: the instructor should mark the link lost, show no fresh climb and silence
   remote audio. Its own button clicks should still work when instructor volume is enabled.

These initial audible checks used the earlier sampled-speaker implementation. The climb-driven
engine above replaces that audio path, excludes student UI effects, and removes dependence on
student volume. Instructor custom-profile configuration and mobile-app input remain outstanding.

## Local-synthesis validation, 2026-10-24

The climb-driven instructor image is `c875d714adbba4c8480a4eac7de0848e02261d8d5eca112a5b7d1e62d110ce1a` (SHA256). It was flashed and
verified in both OTA slots on the same v3.2.7 instructor. The v3.2.5 student retained its existing
binary; V1 compatibility restored the saved pairing without changing either device's group.

- Protocol, pure audio and sanitizer tests passed. Golden waveforms cover default pitch/cadence,
  12.5 Hz local pulses between 4 Hz RF samples, a missed packet without phase reset, stale/invalid
  climb silence, focus/session changes, local mute/effects, legacy PWM exclusion and clock rollover.
- The simulator exercised the real production Speaker: golden cadence, pitch smoothing, continuous
  climb/sink, local sound-effect priority/resume and mute passed after extracting the shared engine.
- `leaf_3_2_6_release`, `leaf_student_3_2_5_training` and `leaf_instructor_3_2_7_training` builds passed.
  The instructor uses 35,832 bytes of static RAM and 658,536 bytes of flash, increases of 88 and
  836 bytes respectively over the immediately preceding instructor build. Runtime CPU/current
  qualification remains outstanding.
- The linked instructor still excludes local flight-sensor, normal settings and normal Speaker
  services; it uses only the shared pure synthesis code. Device status advanced from 139 to 219
  accepted samples in five-second increments of 20, with zero radio errors and valid climb.
  Those captured readings were near zero, correctly producing silence in the default deadband.

The operator subsequently confirmed that the climb-driven audio works and sounds good on the
paired devices. The instructor no longer consumes student PWM or student volume. Student profile
transfer is explicitly out of scope; instructor-owned profile configuration is future work.
Measured end-to-end response and behavior under RF loss still need qualification.

## Student field controls, 2026-10-24

Student v3.2.5 `f0:f5:bd:56:16:a0` was flashed in both OTA slots with SHA256
`04211a3eb36009b644de9d21e34d11f4bcfcb3910eb7bcb09b6f292511ec8ebc`.
Both slot hashes were verified. The app USB port re-enumerated after the normal soft nudge;
the flashable port was detected again and the retry succeeded. Settings were not erased.

- Final student build: 105,508 bytes static RAM, 2,407,976 bytes reported flash usage. Compared
  with the earlier student build (105,348 / 2,379,940), these are +160 / +28,036 bytes. Static RAM
  does not include the restored normal FANET protocol/task heap allocations; measure those and
  stack/CPU headroom under matched runtime workloads before making the production-mode decision.
- Normal `leaf_3_2_6_release` and `leaf_instructor_3_2_7_training` regression builds passed. Only
  the student was flashed during this step; the instructor build artifact is not its installed image.
- Protocol, student enrollment and audio host tests passed. Enrollment tests reject wrong target,
  session, nonce, group, slot and unapproved confirmation; verify cancellation/timeout, replay,
  rollover, retained pairing, and control-frame corruption. Address/undefined-behavior sanitizers passed.
- The real Leaf Labs/Training menus passed host tests and screenshot inspection, including eight
  repeated toggle cycles, resuming requested FANET, not restarting FANET after its lifecycle ended,
  and migrating a saved bench pairing. These use an explicit radio lifecycle stand-in, not RF.
  Reproduce in the gcc container with `make -C sim -j8 TRAINING=1 BUILD=/leaf/sim/build-student
  test-training-student-ui`; pure enrollment tests use `make -C sim test-training-student`.
- On hardware, Training Off was saved before reboot and subsequently reported `on=0`, group 7101,
  slot 0 and instructor `594362e6`. Training On was restored and another reboot confirmed `on=1`
  with the same group/slot/instructor. The device was in its normal USB charging state during
  these captures, so training correctly did not take radio ownership (`rf=0`, `own=0`, `pwr=2`).
  Active RF handoff, local flight behavior and button response on the physical student still need
  an operator-powered-on check; loaded FANET TX/RX transitions and new end-to-end discovery remain
  qualification work. The matching instructor enrollment UI/protocol is pending.
- Student diagnostic lines were shortened after a live capture showed its USB buffer could not
  fit the expanded status line. Periodic logs remain best effort and are dropped rather than
  waiting for a host; no task-manager slots were changed.

Local captures: `.pio/training-student-menu-*.log` and
`.pio/training-student-3-2-5-device.json`. They are ignored local evidence, not published reports.

## Instructor field discovery, 2026-10-24

Instructor v3.2.7 `48:ca:43:59:ae:a8` was flashed in both OTA slots with SHA256
`06f83f35c076e5e480ec7d97435c390db407624e0233bcf565a8ee00ef65c23f`.
Both slot hashes were verified without erasing settings. The installed v3.2.5 student image above
already implements the matching control frames; another student flash is not required.

- `leaf_instructor_3_2_7_training` builds successfully: 35,960 bytes static RAM and 662,868 bytes
  reported flash usage, +128 / +4,332 versus the previously flashed climb-audio instructor image.
  Discovery uses four fixed candidate entries and bounded control traffic; these size comparisons
  do not replace burdened runtime heap/CPU/stack measurements.
- `make -C sim test-training test-training-student test-training-instructor test-training-audio`
  passed. The new suite runs both actual enrollment state machines through the wire codec,
  including lost Offer/Confirm frames, explicit approval, commit-before-confirm, duplicate Accept,
  full rosters, known-slot reuse, stale candidates, cancellation, restart/session isolation and
  rollover. The final retry interval also passed address/undefined-behavior sanitizers.
- `make -C sim -j8 test-training-instructor-ui` passed and produced screenshots of the actual
  pairing renderer in the searching/found/offer/confirmation states. Text bounds and the rendered
  screens were checked at the instructor's 96 x 192 layout. This does not simulate RF or GPIO presses.
- A 43-second device capture retained group 7101 and focus 0, with `radio=1`, `control=1`, high
  volume 3, error code/count zero and polling advancing by 20 transmissions per five seconds.
  No student packets arrived in that capture, so fresh climb/audio were correctly silent.
  Subsequently, the operator completed the button pairing flow and confirmed the instructor
  returned to a live student row. This confirms discovery, student confirmation and live delivery
  between student `56bd55e6` (v3.2.5) and instructor `594362e6` (v3.2.7), without USB setup commands.
  RF-loss/retry and reboot behavior during enrollment, additional students and four-student RF
  capacity still need hardware qualification. Existing bench IDs are migrated when the new atomic
  roster record is absent.
- The RF power/modulation profile and production audio algorithm are unchanged. Normal Bluetooth
  advertising/output and medium-minimum startup volume remain enabled; actual app qualification
  is still outstanding. Pairing traffic uses only idle/unpaired or pending-peer polling slots and
  jittered retries; other paired students retain their scheduled polls.

Local evidence is under `.pio/training-instructor-discovery-*.log`; the current image and prior
image evidence are separated in `.pio/training-instructor-3-2-7-device.json`. The operator's
successful pairing/live-data confirmation is recorded separately from the initial boot capture.
This is a two-device bench result, not a firmware release or a four-student qualification.

## Verification and next work

Run the pure host tests with `make -C sim test-training test-training-student test-training-instructor test-training-audio` in the existing gcc container. The test
covers wire round trips and units, single-bit corruption throughout a telemetry frame, malformed
sizes/fields, pairing and poll-window rejection, four-slot state isolation, request allocation,
rollover, stale values and NMEA checksums/coordinate rounding. The audio suite checks golden
production waveforms, 12.5 Hz local beep cadence from 4 Hz samples, missed packets, stale/invalid
climb, focus/session reset, mute, ignored legacy tone/SFX data and rollover.
`make -C sim test-vario-audio-production` also exercises the actual production Speaker and virtual
PWM output, including sound-effect priority/resume, continuous climb/sink and mute. Sanitizer execution is also useful:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc/vario \
  sim/tests/training_link.cpp src/vario/training/protocol.cpp -o /tmp/training-test
/tmp/training-test
```

The display shows an advancing `RX` counter. Runtime status lines also report remote climb
(cm/s), locally generated vario tone (Hz), configured PWM output (Hz), freshness and gain-control status.
The USB `beep` command requests a 750 ms local 1 kHz test at the selected volume.
Runtime status lines report radio initialization/error code, RX, TX starts and rejected/accepted
traffic as applicable. TX starts are not delivery acknowledgments. Existing student runtime
capture attributes training service time to Services without changing task-manager slots.

Still required: longer two-device runs; audible gain/cadence comparisons and unused-sensor fault
tests; bounded board-control fault tests; app tests
in XCTrack/SeeYou Navigator; source-switch/reconnect tests; four actual transmitters; bounded
resource captures and latency/airtime/current/range measurements; complete setup UI and automatic
recovery; student battery/flight-state fields; and the production student-mode packaging decision.
Audio now runs a local production envelope from the latest fresh climb value. Compare its
perceived response under changing climb and RF loss; instructor-owned profile configuration
remains future work. Do not infer exact phase or end-to-end timing from host waveform tests.

Group/session IDs, explicit enrollment and CRC provide bench filtering, **not authentication or
encryption**. Authentication/replay policy and a qualified RF profile remain necessary product
work. No firmware release or operational four-student capability is claimed by this prototype.
