---
title: Student and Instructor Training Link
description: Two firmware roles for a four-student LoRa training link, focused audio and Bluetooth output, and staged prototype qualification.
---

# Student and instructor training link

Status: proposed; requirements updated 2026-10-24 on `codex/lora-fanet-range`.
The training transport, instructor application and phone integration are not implemented.
The bench diagnostics below establish useful hardware evidence only.

## Agreed scope

Build two firmware loads from shared protocol and Leaf components:

| Firmware role | Required behavior | Packaging decision |
| --- | --- | --- |
| Student | Operate as a normal flying Leaf, pair with an instructor, and transmit selected sensor/flight values without compromising local instruments or audio. | Start with a distinct training build for evaluation. Retain the option of a selectable production training mode if measured CPU, memory, timing and battery costs are acceptable; otherwise ship a separate binary. |
| Instructor | Boot without onboard sensors or an I2C bus, receive up to four paired students through one LoRa radio, offer basic setup/focus controls, and reproduce the focused student's audio and Bluetooth flight-data output. | Dedicated instructor binary with its own boot/task composition and firmware identity; never require normal sensor initialization to succeed. |

- United States first. A dedicated training RF mode is acceptable; simultaneous normal FANET
  tracking is not required for the initial implementation.
- The instructor's Leaf display is primarily for pairing/configuration, a four-student roster,
  focus selection, link freshness and fault status. A paired mobile app supplies the advanced map.
- Exactly one focused student supplies the instructor speaker and the Bluetooth ownship stream.
  Other paired students continue sending at a lower background rate. One radio services all four;
  achievable rates and range remain to be measured.
- Preserve the earlier USB/web-map idea as a later consumer of the same four-student data model,
  not a prerequisite for the first two-device prototype. An off-the-shelf receiver can be evaluated
  later against the same protocol; the first receiver uses a Leaf.

## Work item 1: shared RF protocol and scheduling

- [ ] Define a versioned binary protocol over the existing LoRa hardware, separate from FANET
  framing and timing. Reuse the SPI/radio driver where practical, with explicit mutually exclusive
  radio ownership. Compare candidate PHY settings using measured airtime, delivery and freshness.
- [ ] Resolve the US operating profile against the exact module, antenna, power and applicable
  operating requirements before choosing the final frequency/bandwidth/channel-access policy.
  Do not assume the current FANET profile or a private sync word establishes suitability.
- [ ] Define pairing, stable device IDs, instructor/session ID, protocol capabilities and a bounded
  four-student roster. Require an explicit pairing action and reject unpaired or wrong-session
  traffic. Decide key provisioning/authentication and replay handling before field qualification;
  device IDs or a sync word alone are not authentication.
- [ ] Include packet type/version/length, source/session, sequence, sample age/time and field
  validity. Use explicit units, byte order and bounded fixed-size encoding, not raw C++ structures
  or serialized bus internals. Reject malformed, duplicate and obsolete samples.
- [ ] Send an allowlisted snapshot of bus-derived values: latitude/longitude, GNSS altitude,
  pressure/barometric altitude, climb rate, ground speed, course, fix/time validity, flight state
  and useful student status such as battery. Keep altitude reference and sensor validity explicit.
  Do not stream GPS NMEA characters or raw high-rate IMU samples over RF.
- [ ] Start the airtime model with roughly 32-48 bytes of telemetry payload, then finalize the
  actual encoded size including identity, timing, integrity/security and optional audio fields.
  Budget preamble/header/coding overhead, control/downlink traffic, guard times and retries too.
- [ ] Provisional rates: focused climb/audio values at 4 Hz, background snapshots at 1 Hz per
  other student, and position at the source's actual GNSS cadence (initially about 1 Hz).
  These are development targets, not guaranteed capacity. Do not manufacture fresh position
  fixes by repeating an old sample at 4 Hz.
- [ ] Prefer an instructor-coordinated slot schedule with bounded discovery/control windows
  and one focused high-rate allocation. Account for half-duplex RX/TX, clock drift, missed beacons,
  newcomers and restart recovery. A focus grant expires unless renewed, so an unreachable old
  student cannot retain a conflicting fast slot indefinitely. Define a bounded fallback schedule.
- [ ] Use latest-value telemetry rather than retransmitting a growing history. Bound any control
  acknowledgments/retries and expose effective rates, loss, sample age, RSSI/SNR and TX failures.
  Agree stale/lost thresholds and degraded-rate behavior before field tests.

## Work item 2: student firmware

- [ ] Add a bus subscriber/adapter that copies only the latest selected flight values into a
  fixed-size snapshot. Sensor/message-bus callbacks must not wait for RF, allocate per packet,
  format text or perform SPI transactions.
- [ ] Serialize/transmit in bounded scheduled work, with preallocated buffers and limited control
  processing. Training transport never feeds its received control messages back into local sensor
  data or changes the student's instrument/audio configuration.
- [ ] Add pairing, training status, link/focus state and a way to leave training mode. Instructor
  loss must not interrupt normal local instruments, audio, logging or flight operation.
- [ ] Start with transmitted climb/audio-driving values and the student's relevant audio settings.
  To match what the student actually hears, evaluate compact tone/envelope commands (frequency,
  on/off, cadence and generation timestamp) if reconstructing from reduced-rate climb data differs
  materially. Reuse the tone engine; do not stream microphone recordings or PCM audio.
- [ ] Measure training off/on under matched GPS/IMU/barometer, audio, display, SD and BLE workloads.
  Record task CPU/time, missed deadlines, heap/largest block, stack headroom, queue drops, flash/RAM
  size and battery/current impact. Define numeric budgets before the production-mode decision.
- [ ] Decide selectable production mode versus separate student binary from that evidence.
  With training off, ensure transport tasks/queues and active radio work are released or absent.

## Work item 3: instructor firmware and sensor-free board support

- [ ] Create a distinct instructor build target/identity, shared protocol library and minimal boot
  path. Do not start barometer, IMU, ambient sensor or local GNSS tasks; do not wait for their
  readiness or run the local Kalman estimator. Avoid local ownship/flight logging assumptions.
- [ ] Make the firmware genuinely independent of I2C: no `Wire.begin`, IO-expander initialization,
  polling, retries or hidden I2C access through power, speaker, GPS, SD-detect or diagnostic code.
  Display and LoRa use shared SPI with bounded locking; buttons, audio timing, power latch,
  backlight and supported battery sensing use the applicable direct GPIO/ADC paths.
- [ ] Audit non-sensor expander dependencies before claiming the faulty board is fully usable.
  On v3.2.6/v3.2.7, `SPEAKER_VOLA/B`, charging/status signals, SD detect and GPS controls are mapped
  through the I2C expander. The maintainer's selected prototype approach (2026-10-24) is to solder
  the speaker gain controls for **fixed maximum volume**, with firmware mute/unmute and no
  expander-controlled volume setting. The modification is authorized/planned, not yet verified.
  Existing power-line pull-ups/pull-downs are expected by the maintainer to provide sufficient
  hardware defaults without the expander. Retain those defaults and verify boot, USB/battery
  operation, charging and shutdown; do not assume unavailable expander status can still be read.
  Do not silently reintroduce I2C to satisfy these services. SD storage must not be a boot
  requirement; use existing onboard nonvolatile settings for pairing/configuration.
- [ ] Keep four fixed-size per-student state records with independent sequence/time/validity,
  link statistics, audio configuration and last sample. A background packet cannot overwrite
  the focused student's active state. Do not publish all students as one ownship bus stream.
- [ ] Reuse the bus architecture through typed remote telemetry messages and an adapter exposing
  only the selected student's validated values to existing UI/audio/BLE consumers. Adapt consumer
  interfaces where needed rather than pretending local sensors are initialized or sending remote
  climb values back through the pressure/Kalman pipeline. Prevent forwarding loops.
- [ ] Basic UI: pair/remove/name students, show up to four entries and their freshness/link state,
  select focus, show requested versus confirmed fast rate, set speaker mute and Bluetooth,
  and configure the applicable RF profile. The prototype has fixed maximum hardware gain;
  do not offer a nonfunctional volume control. Restore pairing after reboot; start safely without
  a fix.
- [ ] Make focus changes atomic across active snapshot, audio, BLE queues and scheduler generation.
  Flush old-student pending output and silence old tones before activating the new student. Send
  the new rate allocation and show its acknowledgment/timeout independently of local selection.
- [ ] On stale/lost data, silence vario audio and mark/suppress invalid ownship telemetry; never
  continue plausible sounding climb tones or claim a stale coordinate is a fresh valid fix.
  Show which student is selected and its data age even if the phone app cannot display that status.

### Bluetooth output for the focused student

- [ ] Reuse Leaf's BLE Nordic UART service and existing sentence conventions. Generate checksummed
  GGA/RMC GPS sentences from the selected student's decoded position/time/fix data, plus LK8EX1
  vario output from that same student's pressure, altitude and climb data. Carry the necessary
  validity/fields over RF; never substitute the instructor's position, pressure or battery silently.
- [ ] Refactor the current `BLE::sendVarioUpdate()` dependency on local `baro` readiness and values,
  and provide the generated GPS data to the existing output path without local GPS hardware.
  Confirm unknown-value and stale/fix-invalid handling for each target app.
- [ ] Present the focused student as the phone app's external ownship GPS/vario source. Validate
  that the app actually uses the remote GPS rather than the phone's location. One selected stream
  is required; showing all students simultaneously in a third-party app is outside this first scope.
- [ ] Test XCTrack (from the earlier request) and SeeYou Navigator with recorded app/OS versions,
  connection setup and supported input profile. The latest request also says "xctracer"; confirm
  the exact intended app/product when qualifying that compatibility rather than assuming it is
  another name for XCTrack or that existing Leaf compatibility proves this new source works.
- [ ] Test BLE reconnect, stale input, focus switching with different coordinates/altitudes, queue
  flushing, units/checksums and return of a valid fix. If an app retains old tracks or cannot
  represent a source switch, document the limitation and an explicit session/reconnect procedure.

## Hardware evidence and prototype selection

The tested v3.2.7 has a working GPIO41 input positive control, but grounding SDA/SCL through
100 ohms did not register on GPIO8/9 even with the physical lines measured near 0.119 V.
With temporary jumpers **SDA to GPIO42 and SCL to GPIO41**, two scanner runs found addresses
`0x20`, `0x38` and `0x77` at 10/100/400 kHz, also confirmed by a software scan. No IMU responded.
This supports an original GPIO8/9 connection/input fault and a responsive remaining sensor bus;
it neither identifies the failed physical connection nor qualifies display/radio/audio/BLE.

Bench result record (2026-10-24, USB serial device `34:CD:B0:36:F3:80`):

| Check | Run 1 | Run 2 |
| --- | --- | --- |
| External pull-ups, no internal bias | SDA/SCL HIGH in 128/128 samples | SDA/SCL HIGH in 128/128 samples |
| Independent open-drain pull/release | Pass | Pass |
| Hardware scan, each of 10/100/400 kHz | 3 ACK, 109 NACK, 0 other errors | 3 ACK, 109 NACK, 0 other errors |
| Independent software scan, approximately 10 kHz | 3 ACK, 109 NACK | 3 ACK, 109 NACK |

All scans found the same three addresses listed above. Original GPIO8/9 remained inputs without
internal pulls. Diagnostic image SHA256:
`7b86b70f48898531d8bf3df23c59e81227db9e8729f1eda2bc2f54762327e235`.
This record preserves the result independently of the temporary diagnostic project/capture files.

Use this board as the first instructor prototype after verifying its radio/display/audio paths.
Its I2C jumpers are diagnostic aids, not dependencies of the instructor design. Qualify final
receiver firmware with them removed and with I2C unavailable. Use a healthy LoRa-equipped Leaf
for the student. The v3.2.6 with a bad IMU can be an additional instructor/fault-test unit;
barometer-only operation does not demonstrate normal student IMU behavior.

## Ordered implementation and test plan

1. **Hardware and protocol feasibility.** Inventory one healthy student Leaf and the v3.2.7
   instructor candidate, checking both radios and antennas. Prove instructor SPI display, buttons,
   power latch and audible output without I2C; apply/verify the agreed maximum-volume speaker
   modification and verify the existing power-line resistor defaults as described above.
   Specify packet fields, validity, pairing/session rules and measured US airtime/capacity budget.
   Freeze initial rates and numeric resource/freshness targets before implementing the scheduler.
2. **Small shared transport and automated coverage.** Implement a bounded codec, student snapshot
   adapter and instructor per-student state/focus adapter. Test round trips/units/limits, malformed
   lengths, wrong versions/sessions, duplicates/reordering, reboot/sequence wrap, stale samples,
   control loss and bounded queues using host tests and the existing simulator/message injection
   where practical. Keep these tests independent of the RF driver.
3. **One real student and one real instructor.** Build and flash the two distinct targets. Send
   timestamped bus-derived flight values over RF, first without BLE or complex UI. Verify valid
   sensor data and local vario operation on the student, and decoded values on the instructor.
   Exercise pairing, both-end reboot, RF loss/recovery and no-GPS startup. Record sample-to-receive
   latency, loss, achieved cadence and SPI/display contention. Boot and run the receiver with I2C
   disconnected/held low and no SD card; it must have no sensor waits or I2C retries.
4. **Focused experience and phone integration.** Add the minimal UI, selected-source bus adapter,
   speaker and BLE output. Replay known climb/sink/audio sequences to the student, compare student
   and instructor tones/timing, and verify correct remote position/vario in at least one actual
   target app before expanding compatibility. Drop the RF link and verify freshness/audio behavior.
   Record app/OS settings, transmitted/decoded values, latency and reconnect results.
5. **Four-student behavior.** Use four simulated identities to test state isolation and roster/UI
   limits, then exercise actual RF contention with additional radios/independent transmitters.
   Two Leafs alone do not validate four-aircraft RF performance. Verify one focused allocation,
   background updates, focus handover/lease expiry, missed beacons, a restarting student, an
   unpaired fifth student, distant links and BLE output that never mixes sources. Finish with four
   real students/transmitters before advertising four-student operational support.
6. **Resource, range and product decision.** Compare student training off/on and worst-case
   instructor RX/display/audio/BLE loads with bounded runtime captures and longer stability runs.
   Measure current/battery impact, latency distributions and loss under interference and heading
   changes, then progress from ground tests to supervised air-to-ground evaluation. Record the
   geometry and antennas rather than inheriting an air-to-air range claim. Decide production
   student mode versus separate binary; retain dedicated instructor packaging either way.

## Acceptance evidence to retain

- [ ] One healthy student and one sensor-free instructor run together, with exact source commit,
  firmware roles/hashes, board identities, RF/antenna settings and hardware modifications recorded.
- [ ] Instructor boots/operates without I2C or onboard sensor readiness, with verified usable
  display/buttons, LoRa, speaker and BLE. Normal student behavior remains correct on RF loss.
- [ ] Provisional 4 Hz focused / 1 Hz background operation is either supported by measured airtime,
  freshness and loss results or replaced by documented achievable rates. Agree latency and stale
  limits before qualification; sample age remains visible and stale audio does not continue.
- [ ] A real mobile app shows the selected student's position/vario, including loss/recovery and
  reconnect. Use at least two distinct source tracks to prove clean focus changes; record each
  requested app's actual compatibility separately.
- [ ] Four-student capacity has physical RF evidence in addition to host/simulator tests; do not
  mark that capability complete based on the first two-device prototype.
- [ ] Matched resource reports establish production-student packaging and acceptable receiver
  headroom. Record known limitations and failed tests. If task-manager slots change, synchronize
  exported CPU labels as required by `AGENTS.md`. Any later publication follows the normal
  [release](../releasing.md) and [runtime assessment](../runtime-assessment.md) requirements.

## Related implementation and backlog references

- [Compatible FANET range and antenna work](lora-fanet-range-and-antennas.md): share measured RF
  findings while keeping its interoperable FANET mode separate from this dedicated training mode.
- [Existing radio driver](../../../src/vario/comms/fanet_radio.cpp) and
  [shared SPI implementation](../../../src/vario/hardware/Leaf_SPI.cpp).
- [Message bus](../../../src/vario/dispatch/message_bus.h),
  [message types](../../../src/vario/dispatch/message_types.h) and
  [shared injection parser](../../../src/vario/dispatch/message_injector.h).
- [BLE output](../../../src/vario/comms/ble.cpp),
  [speaker control](../../../src/vario/ui/audio/speaker.cpp),
  [power initialization](../../../src/vario/power.cpp) and
  [v3.2.7 hardware mapping](../../../src/variants/leaf_3_2_7/variant.h).
