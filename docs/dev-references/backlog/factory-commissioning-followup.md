---
title: Factory Commissioning Follow-up
description: Future hardening notes for factory_interface commissioning and flashing behavior.
---

# Factory Commissioning Follow-up

Status reconciled on **2026-10-21** with the v3.2.8 hardware-support work. The implementation
and hardware evidence are recorded in the
[IMU migration backlog](st-imu-magnetometer-migration.md#flat-commissioning-bias-gps-records-and-audio-restoration-2026-10-21).
Test commissions used a disposable in-memory database; production records were not changed.

The factory flash flow currently erases the NVS partition before writing firmware so previously
commissioned units can be recommissioned. The current Leaf partition table uses the standard Arduino
ESP32 NVS range:

- Partition: `nvs`
- Type/subtype: `data`, `nvs`
- Offset: `0x9000`
- Size: `0x5000` (`20K`)

This was verified against both the configured `default_8MB.csv` partition table and the generated
`leaf_3_2_6_release` `partitions.bin` artifact.

Future work:

- Replace the factory_interface hardcoded NVS erase range with partition-table-derived values.
- Prefer parsing the configured/generated partition table and erasing the partition named `nvs`.
- Keep the current hardcoded range acceptable while Leaf continues to use the standard
  `default_8MB.csv` layout.

## Commissioning Feature Backlog

### GPS satellite test: suspend Auto-Off

Status: Implemented; dedicated Auto-Off timeout verification remains pending.

The self-test update resets the inactivity counter while tests are running, including the long
GPS fix test. User Auto-Off settings and low-battery shutdown behavior are retained. After tests
finish, normal inactivity timing resumes. Validate with a short configured Auto-Off interval
and a GPS wait longer than that interval, including completion/failure/cancellation.

Acceptance criteria:

- Starting the GPS satellite test prevents Auto-Off from powering down the unit.
- Auto-Off remains suspended until the test completes, fails, or is cancelled.
- Leaving the test restores the Auto-Off behavior that was active before the test started.

### GPS satellite test: mute vario audio

Status: Implemented and checked in the simulator and on the v3.2.8 test board.

Vario audio is muted during the GPS fix test and remains quiet on awaiting-confirmation and
COMPLETE/results pages. Removing the final page restores configured vario volume, including
Off; covering it with a modal or receiving commissioning confirmation does not unmute it.
Starting another self-test clears the previous test's temporary mute.

Acceptance criteria:

- Starting the GPS satellite test silences vario audio.
- Vario audio remains muted through test completion/failure/cancellation and the final results page.
- Dismissing the final page restores normal configured audio on the functioning vario pages.

### GPS fix result: record satellites from the passing observation

Status: Implemented and verified with parser/self-test regression checks and hardware tests.

PASS requires a new checksum-valid GGA received after test start, with nonzero fix quality
and satellites used, no older than three seconds. `Fix_sats` is copied from that same GGA.
GSV/display satellite counts may differ or still be zero; stale GGA, RMC alone and no-fix
GGA cannot pass. The final diagnostic commission recorded six satellites used while the later
display count was eleven, and all tests passed.

### Flat acceleration-bias capture

Status: Implemented and hardware-verified for v3.2.8 only.

After electrical/gyro checks, capture 100 normal-operation acceleration/gravity pairs during
the existing warm-up while Leaf remains flat and still. Validate the sample window, save a
versioned NVS reference with immediate readback, and record concise `FlatBias_g`/`Saved` notes.
Reboot tests confirmed restoration and runtime Kalman initialization from the saved reference.
It is a startup seed for similar mounts; the runtime estimator continues learning. It does not
provide full three-axis accelerometer calibration.

Normal desk typing passed repeated measurements. A stronger deliberate tapping test verified
automatic recovery: discard rejected gyro windows and try up to three complete windows with
the original accuracy limits. Persistent disturbance still fails. The additional attempts need
no new operator step; the driver watchdog is 25 seconds within the 30-second outer IMU timeout.
Keep more extensive vibration, temperature and multiple-board characterization as follow-up work.

Potential follow-up: migrate v3.2.7 and earlier ICM hardware to the same Kalman bias estimator
and capture a flat reference during its existing commissioning step. Current ICM estimation
and calibration remain unchanged. Define missing-record compatibility and storage/versioning,
avoid applying both old and new corrections, and validate representative ICM units before
enabling this option. See the
[ICM migration backlog](vario-imu-gravity-performance.md#optional-icm-estimator-migration-deferred).
