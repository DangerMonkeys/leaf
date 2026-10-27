---
title: Student and Instructor Training Link
description: Two firmware roles for a four-student LoRa training link, focused audio and Bluetooth output, and staged prototype qualification.
---

# Student and instructor training link

Status: initial bench implementation in progress; requirements updated 2026-10-26.
Transport, instructor UI and Bluetooth output have initial implementations; hardware and
phone-app qualification remain outstanding.

## Initial implementation, 2026-10-24

A first bench prototype is in progress on `codex/instructor-student-link`, in a separate worktree
based on `89d938cf`. See [build/setup and wire-format notes](../training-link-prototype.md).
It adds v3.2.5/v3.2.7 student and v3.2.6/v3.2.7 instructor build targets, a bounded codec and polled RF
owner, student bus-derived snapshots,
four receiver records/focus selection, an instructor boot path independent of local flight sensors,
normal I2C-expander speaker gain control, climb-driven production vario audio and BLE NMEA output. Host tests exercise the protocol and receiver model. The checklists below remain the
full product scope: sustained two-device and four-transmitter operation, app compatibility, exact audio
cadence, hardware power/speaker behavior, resource budgets and production RF/security qualification
are not established by these implementation/build tests.

A first v3.2.5 student / v3.2.7 instructor bench check now receives focused telemetry at nominal
4 Hz, with working normal expander volume control. Restart restores medium-enabled volume and
the saved pairing. See the prototype guide's bench record for binary hashes and limits; actual
phone-app, audible-fidelity, sensor-fault and capacity qualification remain outstanding.

## Agreed scope

Build two firmware loads from shared protocol and Leaf components:

| Firmware role | Required behavior | Packaging decision |
| --- | --- | --- |
| Student | Operate as a normal flying Leaf, pair with an instructor, and transmit selected sensor/flight values without compromising local instruments or audio. | Start with a distinct training build for evaluation. Retain the option of a selectable production training mode if measured CPU, memory, timing and battery costs are acceptable; otherwise ship a separate binary. |
| Instructor | Run on standard LoRa-equipped Leafs using normal board controls, including I2C-expander speaker volume, without depending on local flight sensors. Receive up to four paired students through one LoRa radio, offer basic setup/focus controls, and generate the focused student's vario audio and Bluetooth flight-data output. | Dedicated instructor binary with its own boot/task composition and firmware identity; never require normal sensor initialization to succeed. |

- United States first. Student Training lives under Settings > Leaf Labs. Training and FANET
  use the radio exclusively: training enabled suspends normal FANET; training disabled restores
  normal FANET behavior according to the user's existing settings. Local instruments, audio, logging
  and Bluetooth continue normally in either mode. Simultaneous FANET/training is out of scope.
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
- [x] Apply the SX1262 power/current setup in the shared training radio: request 22 dBm and
  set/verify 140 mA after initialization/PA setup in both roles. Keep SF7/BW500/4/5 unchanged
  for comparison. Actual RF output, current/battery impact and range still need measurement.
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
- [x] Use autonomous telemetry and optional instructor Focus messages. The selected student uses
  about 4 Hz and others about 1 Hz while Focus is recent; after three seconds without control,
  every student returns to 4 Hz. No instructor reply is required for any individual sample.
- [ ] Qualify jitter/collisions with four physical students, including simultaneous startup and
  control loss (up to sixteen data frames/s). Revisit CAD or clock coordination if required,
  while retaining autonomous transmission when instructor control is unavailable.
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
- [x] Add Settings > Leaf Labs > Training on the student, with Enabled, Pair and Unpair, plus
  saved instructor identity and current link state. Store enable separately from pairing; disabling
  stops training RF work and preserves enrollment, and re-enabling reconnects without re-pairing.
  Persist enable state and enrollment across power loss. Student-side discovery/control frames are
  implemented, along with the instructor discovery/offer/confirmation workflow. Pairing requires an explicit action and
  a bounded confirmation window; never silently bind to a different instructor after link loss.
  Unpair explicitly clears enrollment and cancels pending training traffic; require confirmation.
  Distinguish Disabled, Not paired, Connecting and Connected on screen.
- [x] Support normal FANET with training disabled in the same student binary, with runtime
  exclusive radio handoff instead of unconditional student-build FANET exclusions.
  Preserve the user's normal FANET settings rather than overwriting them when toggling training.
- [ ] Implement exclusive radio handoff when toggling training: stop/quiesce FANET TX/RX and
  callbacks before starting training, and stop training traffic/callbacks before restoring normal
  FANET radio configuration and lifecycle. Preserve saved pairing and normal FANET preferences.
  Test repeated toggles, an in-flight packet, reboot in either mode, training without a paired
  instructor, and instructor loss. Do not add simultaneous FANET/training scheduling. Instructor
  loss must not interrupt normal local instruments, audio, logging or flight operation.
- [x] Generate instructor vario audio locally from received climb rates using shared production
  pitch/cadence/smoothing code. Keep beep timing running between 4 Hz updates and across a missed
  packet, and silence invalid/stale climb or old-focus audio. Do not sample student PWM for rhythm.
- [x] Exclude student UI/button sound effects and student volume from instructor vario generation.
  Retain local instructor button clicks. Existing V1 student binaries remain compatible; the old
  tone field is ignored and new student builds leave it zero.
- [ ] Add instructor-owned sound-profile and threshold configuration, reusing production profile
  validation and controls where practical. The first local engine uses production defaults. Do
  not transfer student profiles: the instructor should use its own familiar sound settings,
  independently of student volume, quiet mode and UI sounds.
- [ ] Measure training off/on under matched GPS/IMU/barometer, audio, display, SD and BLE workloads.
  Record task CPU/time, missed deadlines, heap/largest block, stack headroom, queue drops, flash/RAM
  size and battery/current impact. Define numeric budgets before the production-mode decision.
- [ ] Decide selectable production mode versus separate student binary from that evidence.
  With training off, ensure transport tasks/queues and active radio work are released or absent.

## Work item 3: instructor firmware on standard boards

- [x] Replace per-update polling with autonomous student broadcasts. Default/fallback 4 Hz,
  optional instructor Focus at 1 Hz to reduce other students to 1 Hz for a three-second lease,
  and two-second audio/data hold. Keep pairing/name persistence, per-source sequence isolation
  and the message-bus adapters. Both firmware roles need updating.
- [x] Request 22 dBm and set/verify the 140 mA current limit in the shared training driver.
- [ ] Re-test airborne-to-ground range, actual RF output/current and one-way control loss on the
  new autonomous/high-power images. Exercise four physical transmitters with collision/focus
  changes and no instructor control; software tests do not qualify that RF workload.

- [ ] Refine UI usability following the 2026-10-25 operator test: live pairing/name sharing worked,
  but the interaction was confusing. Gather the unclear steps before revising the flow. Make the
  altitude-source unavailable state explicit instead of labeling missing data GPS.

- [x] Use the student's existing active pilot profile as the instructor label, with no separate
  keyboard/input path. Read the profile only during pairing; send a bounded UTF-8 label in the
  discovery announcement and persist it on the instructor. No student name cache, periodic
  metadata requests or profile-edit hooks. Pair again to update a changed name. Retain ID/session
  routing; the student retains only compact offer fields, never a whole packet/name buffer.
  Show names in discovery, roster, focused instruments and unpair confirmation, with ID fallback
  for missing profiles/older firmware and IDs available for duplicate-name identification.

- [x] Replace the initial text dashboard with a compact Thermal Core-inspired instrument panel,
  using Leaf fonts and rounded inverted cursor rows. Put four student slots and Settings below;
  browsing preserves the active source until selection. Move volume, unit choices and linking
  into Settings, persist display units locally, and default unpair confirmation to Cancel.
  Preserve independent GPS/pressure freshness and label the transmitted pressure altitude BARO.
  Course/speed are GPS ground values; unavailable wind/thermal data are not fabricated.

- [x] Improve instructor readability with 6x12 primary text and 5x8 secondary status/help. Keep all
  four students visible, give focus climb/link/GPS status a dedicated area, and move setup details
  into a link menu. Add explicit removal confirmation instead of a center-hold shortcut.
- [x] Reserve a three-second center hold globally for power. Release the regulator latch on
  shutdown; mute audio/gain, sleep the radio and stop BLE. While USB supplies power, show the off
  screen and accept a separate hold for a clean restart. Consume boot-held and already-triggered
  presses until release so one hold cannot immediately reverse power state or delete a link.
  Simulator checks cover button debounce/hold/release/rollover and actual display glyph bounds.
  USB and battery shutdown/restart, BLE disappearance/reconnect and saved links/volume still need
  verification on the new binary; full battery protection/charging UI remain separate work.

- [x] Add button-based discovery and linking compatible with the student's Leaf Labs menu: show
  up to four fresh candidate IDs, require explicit instructor selection plus student confirmation,
  assign/reuse a roster slot, generate a group on first setup, and persist the group/roster atomically.
  Retry Offer/Confirm without duplicate enrollment, reject stale/wrong-session controls, and never
  evict another student when full. Preserve existing bench settings and normal audio/Bluetooth.
  The operator confirmed successful button pairing and live data on one v3.2.5 student and one
  v3.2.7 instructor. RF-loss/reboot cases and four-student hardware qualification remain outstanding.
- [ ] Create a distinct instructor build target/identity, shared protocol library and minimal boot
  path. Do not start barometer, IMU, ambient sensor or local GNSS tasks; do not wait for their
  readiness or run the local Kalman estimator. Avoid local ownship/flight logging assumptions.
- [ ] Initialize normal board controls using the hardware variant's I2C-expander address, output
  latches and pin directions. Use the expander for speaker off/low/medium/high gain, preserving
  unrelated outputs. Display and LoRa continue to use SPI; do not initialize unused flight sensors
  through these services. Standard boards must not require soldering or bypass modifications.
- [ ] Bound I2C transactions and treat unavailable unused sensors as nonfatal. A missing or
  unresponsive IMU, barometer or ambient sensor must not delay instructor startup or RF processing.
  If the expander itself is unavailable, report the board-control fault and continue display,
  LoRa and BLE without repeated bus retries. Qualify normal USB/battery operation, charging and
  shutdown with the standard board controls; do not rely on resistor defaults as their replacement.
  SD storage must not be a boot requirement; use onboard nonvolatile settings for pairing/configuration.
- [ ] Keep four fixed-size per-student state records with independent sequence/time/validity,
  link statistics, audio configuration and last sample. A background packet cannot overwrite
  the focused student's active state. Do not publish all students as one ownship bus stream.
- [ ] Reuse the bus architecture through typed remote telemetry messages and an adapter exposing
  only the selected student's validated values to existing UI/audio/BLE consumers. Adapt consumer
  interfaces where needed rather than pretending local sensors are initialized or sending remote
  climb values back through the pressure/Kalman pipeline. Prevent forwarding loops.
- [ ] Basic UI: pair/remove/name students, show up to four entries and their freshness/link state,
  select focus, show requested versus confirmed fast rate, set speaker volume/mute and Bluetooth,
  and configure the applicable RF profile. Remember gain, but enable volume on every boot at medium minimum (retain saved high).
  Keep low/mute available during use; stale or absent telemetry must still produce no tone.
  Restore pairing after reboot; start safely without a fix.
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

## Hardware platform and qualification

Use standard, unmodified LoRa-equipped Leaf boards for both roles. Student build targets are
v3.2.5 and v3.2.7; instructor targets are v3.2.6 and v3.2.7. Qualify the expander address, pin
assignments and power controls before adding other hardware revisions. The student requires working flight sensors. The instructor uses normal
board controls but obtains flight values only from its students, so absent or unresponsive local
IMU/barometer/ambient sensors must not prevent operation.

Verify expander-controlled volume on a normal board and separately exercise sensor faults while
board controls remain available. Also test a board-control communication fault: it must be bounded
and reported without blocking display, radio or Bluetooth operation. Full power management and
charging behavior still require qualification independently of sensor independence.

## Ordered implementation and test plan

1. **Hardware and protocol feasibility.** Inventory two standard, unmodified LoRa-equipped Leafs,
   checking both radios and antennas. Prove instructor SPI display, buttons, power latch and
   expander-controlled speaker gain/mute. Verify normal board-control defaults and power behavior.
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
   latency, loss, achieved cadence and SPI/display contention. Boot and run the receiver without
   an SD card and with unused flight sensors missing or unresponsive; it must not wait for sensors.
   Verify volume with a working expander, then separately test an expander communication fault
   for bounded, nonfatal handling without repeated I2C retries.
4. **Focused experience and phone integration.** Add the minimal UI, selected-source bus adapter,
   speaker and BLE output. Replay known climb/sink/audio sequences to the student, compare student
   and instructor tones/timing, exercise all gain levels/mute across focus changes and stale links,
   and verify correct remote position/vario in at least one actual target app before expanding
   compatibility. Drop the RF link and verify freshness/audio behavior.
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

- [ ] One student and one instructor run together on standard boards, with exact source commit,
  firmware roles/hashes, board identities and RF/antenna settings recorded.
- [ ] Instructor boots/operates without unused onboard sensor readiness, with verified usable
  display/buttons, LoRa, expander-controlled speaker gain/mute and BLE. Sensor faults and bounded
  board-control fault handling are tested. Normal student behavior remains correct on RF loss.
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
