---
title: ST IMU and Separate Magnetometer Migration Backlog
description: Hardware v3.2.8 implementation status, bench results, and remaining validation for the LSM6DSVTR, startup, climb processing, power management, and commissioning.
---

# ST IMU and Separate Magnetometer Migration Backlog

## Status and scope

The build and tracing instructions below describe the original hardware migration. The dev firmware configuration and `LEAF_IMU_STARTUP_TRACE` have since been retired. Use `leaf_3_2_8_release` for current builds; historical capture logs can still be analyzed with `capture_startup.py --analyze`.

Status updated on **2026-10-21**. Initial v3.2.8 hardware support is ready for PR review;
flight, temperature, multiple-board and electrical fault/current qualification remain follow-ups.
Core v3.2.8 support is implemented in commit `6ff70157`
(`Add Leaf v3.2.8 LSM6DSV IMU support`). Development/release builds, a v3.2.6 regression
build, and the recorded simulator checks passed. One board passed the proposed resting mean
climb target in all six orientations, and a combined motion test reached 7.06 g without losing
valid output. Orientation changes exposed a 20-30-second bias-settling problem. Physical
moving-start, sustained rotation, temperature, power/fault and flight qualification remain open.
A full test commissioning passed on this
board on 2026-10-21, including POWER and interactive checks; see the commissioning follow-up
below. The earlier POWER failure has not been explained. This is a development checkpoint.

The **2026-10-21 follow-up** adds startup/motion capture, fixes resets caused by
brief acceleration saturation, and gates Kalman bias learning using the existing ICM sample
tolerances. The operating range is now +/-8 g. The first gated fixed-orientation motion test
kept output available and recovered promptly; see the follow-up records below for measured
results, installed-image history and remaining qualification.

The latest commissioning follow-up adds a saved flat-orientation acceleration-bias startup
seed for v3.2.8, coherent GPS satellites-used records, and restoration of vario audio when the
final self-test page is dismissed. Hardware capture/readback and reboot persistence passed;
the operator confirmed the expected audio behavior. Three stationary runs passed, including
two during normal desk typing. A deliberate stronger desk disturbance verified that rejected
gyro windows are discarded and an automatic clean retry can complete the same test. See the
commissioning rejection/recovery record below; older ICM behavior remains unchanged.

The hardware-support branch is `codex/hardware-v3.2.8`, created from `origin/main` at
`82a74de7`. This document was brought forward from `codex/leaf-log-site-link`, where it was
introduced in `26026ae6`. The implementation/measurement record and checkpoint below supersede
the earlier planning sections wherever they differ.

Replace Leaf's TDK ICM-20948 with the ST LSM6DSVTR on v3.2.8, retain the ICM implementation
for older boards, and keep the new driver and acceleration calculation as simple as possible.
The implementation **polls the FIFO**, uses on-chip gravity direction, and does not use
magnetometer measurements. V3.2.8 bypasses the old learned scalar gravity correction and uses
an experimental barometer-observed Kalman acceleration-bias state instead. Measured resting
accelerometer residuals required correction; removal of all correction is not supported by the
current evidence. Older ICM boards retain their existing path.

The confirmed component changes are also recorded in `vario/hardware/hardware_changes.md`.

## Confirmed hardware and requirements

| Item | Confirmed detail |
| --- | --- |
| Hardware version | Leaf v3.2.8 |
| Replacement IMU | ST LSM6DSVTR (LSM6DSV), I2C address `0x6A` |
| Separate magnetometer | MEMSIC MMC5983MA, I2C address `0x30` |
| Sensor mounting | Both chips on top of the PCB; +Y points generally forward relative to the board |
| User mounting | May differ; vertical acceleration must not assume a fixed mounting orientation |
| IMU INT1 | Connected to IO expander P14 (`TCA_P14`) |
| IMU INT2 | Unused |
| Magnetometer INT | Unused |
| Speaker | Same Sky CPT-9019A-SMT-TR; no additional speaker firmware changes required |
| Initial acquisition | Periodic FIFO polling; interrupt routing disabled |
| Future interrupt use | Preserve INT1 for features such as tap/double-tap gestures |
| Magnetometer scope | No magnetic data, compass calibration, heading fusion, or required magnetometer commissioning test |
| Moving startup | Must support being switched on while flying, with a 3-10 second warm-up acceptable |
| Older hardware | Retain the ICM-20948 driver and existing gravity-estimation behavior |

Use the existing I2C bus. Other pins and peripherals are provisionally assumed to match v3.2.7;
confirm any additional board differences during bring-up. The supplied addresses and INT1 route
resolve the earlier open questions about those connections.

## Hardware variant and build support

- Add `src/variants/leaf_3_2_8/variant.h` and development/release PlatformIO environments.
- Select the LSM6DSV at compile time for v3.2.8. Keep existing variants on the ICM path.
- Add `leaf_3_2_8_dev` and `leaf_3_2_8_release` to the CI build matrix.
- Verify hardware-version reporting, firmware filenames, release metadata, OTA selection, and
  factory-interface discovery of local builds and release assets.
- Preserve the existing legacy target of the generic `firmware.bin`; v3.2.8 needs its own asset.
- Confirm the existing flash size, partition layout, and non-application binaries remain suitable.
- Keep the repository's default build environment unchanged unless deliberately selected for change.

The existing variant creation process is described in `src/variants/README.md`.

## Initial Polling Implementation

The current `src/vario/hardware/icm_20948.cpp` driver polls DMP FIFO data through the task manager
at roughly 20 Hz. It requests acceleration and Quat9 orientation. Retain that periodic service
model as the starting point, adapting rates and FIFO parsing for the new device.

ST's [sensor-fusion example](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lsm6dsv_STdC/examples/lsm6dsv_sensor_fusion.c)
polls FIFO status and retrieves quaternion, gravity, and gyro-bias data without configuring either
interrupt pin. This supports the polling approach for onboard fusion as well as ordinary acquisition.

## Required Vario Output

The first firmware goal is not a complete nine-axis orientation solution. Leaf needs a reliable
gravity direction and a gravity-compensated acceleration along world Z for the climb-rate Kalman
filter. Magnetic heading is not required for this calculation.

The current `src/vario/instruments/imu.cpp` path reconstructs quaternion W, rotates raw acceleration
into a world frame, takes world Z, subtracts the learned `gravity_` value, and converts the result
from g to m/s^2 for `kalmanvert_.update()`. With the LSM6DSV, direct projection onto the SFLP
gravity direction can replace the quaternion rotation. This makes yaw drift irrelevant to the
vario and avoids quaternion sign and convention errors.

Preserve this output contract:

- Positive acceleration means upward.
- A stationary Leaf produces approximately zero acceleration in every orientation.
- The value passed to the Kalman filter is in m/s^2.
- Invalid, stale, or unstabilized fusion data must not appear as a plausible nonzero acceleration.
- Barometric altitude remains the long-term climb-rate reference; the IMU supplies fast response.

## SFLP Gravity Vector

SFLP combines accelerometer and gyroscope data internally and produces a game-rotation quaternion,
a gravity vector, and an estimated gyro bias. The gravity vector describes vertical in the IMU's
sensor coordinate frame. It is not already gravity-subtracted linear acceleration and is not a
world-coordinate vector.

The gravity vector is available only through the FIFO as three signed 16-bit components. Its tag
is `0x17` (`LSM6DSV_SFLP_GRAVITY_VECTOR_TAG`), range is +/-2 g, and sensitivity is
0.061 mg/LSB. Raw accelerometer records use tag `0x02` (`LSM6DSV_XL_NC_TAG`). See:

- [LSM6DSV datasheet, Section 2.7, Table 1, and the FIFO/register tables](https://www.st.com/resource/en/datasheet/lsm6dsv.pdf).
- [AN5922](https://www.st.com/resource/en/application_note/an5922-lsm6dsv-6axis-imu-with-embedded-sensor-fusion-i3c-oiseis-for-smart-applications-stmicroelectronics.pdf).
- [AN5763, Section 9.6.3](https://www.st.com/resource/en/application_note/an5763-lsm6dsv16x-alwayson-3axis-accelerometer-and-3axis-gyroscope-with-embedded-machine-learning-core-and-qvar-detection-stmicroelectronics.pdf)
  for the shared SFLP FIFO encodings.
- ST's [LSM6DSV FIFO example](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lsm6dsv_STdC/examples/lsm6dsv_fifo.c).
- ST's [platform-independent LSM6DSV driver](https://github.com/STMicroelectronics/lsm6dsv-pid).

ST does not publish the SFLP filter equations, gains, covariance, exact dynamic transfer function,
or processing latency. Treat it as a proprietary six-axis estimator and characterize it on the
bench and in flight.

### Coordinate and sign contract

Use these definitions at the driver/instrument boundary:

- `a_sensor = [ax, ay, az]`: raw acceleration converted to g in IMU axes.
- `g_sensor = [gx, gy, gz]`: SFLP gravity vector converted to g in the same axes.
- `up_sensor`: normalized gravity vector, pointing upward in the IMU frame.
- `accel_up_g`: net world-vertical acceleration, positive upward, in g.

Apply the same documented sensor-to-board axis transform to acceleration and gravity, or leave
both in sensor coordinates. The dot product is invariant to a common transform, but diagnostics
must not silently mix board and sensor axis labels.

Confirm the sign on real hardware:

1. With IMU +Z upward, raw acceleration and SFLP gravity should both be approximately
   `(0, 0, +1 g)`.
2. With the board upside down, both should reverse.
3. If SFLP uses the opposite convention, negate the gravity vector once in the hardware driver.
4. Verify stationary output is zero, upward acceleration is positive, and a controlled downward
   acceleration is negative.

### World-vertical acceleration calculation

Perform the calculation entirely in sensor coordinates:

```text
gravity_magnitude = length(g_sensor)
up_sensor = g_sensor / gravity_magnitude
measured_up_g = dot(a_sensor, up_sensor)
accel_up_g = measured_up_g - gravity_reference_g
accel_up_mps2 = accel_up_g * 9.80665
```

An initial bring-up implementation can subtract the SFLP magnitude:

```cpp
float gravityMagnitude = sqrtf(gx * gx + gy * gy + gz * gz);

if (gravityMagnitude > 0.8f && gravityMagnitude < 1.2f) {
  float ux = gx / gravityMagnitude;
  float uy = gy / gravityMagnitude;
  float uz = gz / gravityMagnitude;

  float measuredUpG = ax * ux + ay * uy + az * uz;
  float accelUpG = measuredUpG - gravityMagnitude;
  float accelUpMps2 = accelUpG * 9.80665f;
}
```

For v3.2.8 bring-up, bypass Leaf's current learned gravity magnitude, clamps, slew limits, and
recovery heuristics. The equivalent direct projection is:

```text
accel_up_g = dot(a_sensor - g_sensor, normalized(g_sensor))
```

This is the initial candidate, not a guarantee of sufficient production accuracy. Do not assume
the SFLP vector magnitude measures local gravitational strength or corrects accelerometer
offset, scale, and temperature errors. Characterize gravity magnitude and the remaining vertical
residual across orientations, temperatures, and several boards before deciding to ship without
additional correction.

If correction is necessary, distinguish per-axis offset, scale error, local-gravity mismatch,
and fusion-direction error. Choose the smallest justified correction. Evaluate six-position
per-axis calibration if the error is orientation-dependent; retain a learned scalar only if
measurements justify it and its update rules cannot absorb real flight acceleration. Do not
average the first seconds of flying acceleration and call the result gravity or sensor offset.

The earlier blanket recommendation to retain Leaf's learned scalar on the new chip is superseded
by this evaluation plan. Older ICM boards retain their existing estimator. See the
[IMU gravity performance backlog](vario-imu-gravity-performance.md) for the original evidence.

Do not calculate `length(a_sensor) - 1`: that loses the up/down sign and mistakes horizontal
acceleration for vertical acceleration. Do not use physical sensor Z directly because Leaf does
not remain level in flight.

### Proposed configuration and FIFO decoding

Start near ST's reference configuration, then tune from measurements:

- Accelerometer high-performance mode, +/-4 g, 60 Hz.
- Gyroscope enabled, initially +/-2000 dps and 60 Hz. The full-scale choice follows ST's example;
  60 Hz is Leaf's proposed starting rate. Revisit the range after confirming SFLP requirements
  and measuring actual flight rates.
- SFLP at 60 Hz. Test 30 Hz later if lower power is valuable and latency remains acceptable.
- FIFO stream/continuous mode.
- Batch raw acceleration and SFLP gravity at the same nominal rate.
- Initially batch quaternion and gyro bias for diagnostics; remove unneeded streams after validation.
- Use timestamps to establish acquisition timing, alignment, and fusion latency. A timestamp-free
  production path is acceptable only if it still establishes reliable sample timing and age.

These ranges, rates, filters, and diagnostic streams are proposed settings, not confirmed final
tuning. SFLP must run no faster than either the accelerometer or gyroscope. Include all enabled
streams when calculating FIFO production, drain limits, and bus traffic.

Representative driver calls are:

```cpp
lsm6dsv_sw_por(&dev_ctx);
lsm6dsv_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);
lsm6dsv_xl_full_scale_set(&dev_ctx, LSM6DSV_4g);
lsm6dsv_gy_full_scale_set(&dev_ctx, LSM6DSV_2000dps);

lsm6dsv_fifo_sflp_raw_t sflpFifo = {};
sflpFifo.gravity = 1;
sflpFifo.game_rotation = 1;  // Optional diagnostic stream.
sflpFifo.gbias = 1;          // Optional diagnostic stream.
lsm6dsv_fifo_sflp_batch_set(&dev_ctx, sflpFifo);

lsm6dsv_fifo_xl_batch_set(&dev_ctx, LSM6DSV_XL_BATCHED_AT_60Hz);
lsm6dsv_fifo_mode_set(&dev_ctx, LSM6DSV_STREAM_MODE);
lsm6dsv_xl_data_rate_set(&dev_ctx, LSM6DSV_ODR_AT_60Hz);
lsm6dsv_gy_data_rate_set(&dev_ctx, LSM6DSV_ODR_AT_60Hz);
lsm6dsv_sflp_data_rate_set(&dev_ctx, LSM6DSV_SFLP_60Hz);
lsm6dsv_sflp_game_rotation_set(&dev_ctx, PROPERTY_ENABLE);
```

The last API enables the SFLP engine even if gravity is the only retained output. Confirm names
against the exact ST driver revision used in Leaf.

Drain the FIFO with `lsm6dsv_fifo_status_get()` and `lsm6dsv_fifo_out_raw_get()`. Convert raw
acceleration with the helper matching its configured full scale. Convert gravity with
`lsm6dsv_from_sflp_to_mg()` and divide by 1000 for g. Decode the three little-endian signed values
explicitly or with `memcpy` rather than unsafe unaligned pointer casts.

### Timing and pairing

Acceleration and gravity are separate FIFO records. Combining a new acceleration sample with an
old gravity direction during rapid rotation creates false vertical acceleration.

- Preserve FIFO order and associate each acceleration sample with the nearest gravity sample.
- During bring-up, use sensor timestamps or log host times to measure phase and SFLP latency.
- Reject output when gravity is invalid, unstabilized, stale, implausibly small/large, or too far
  in time from the acceleration sample.
- Initially retain approximately 20 Hz host polling and the existing downstream cadence. Drain
  accumulated 60 Hz records and identify temporally matched pairs, rather than combining the newest
  value from each stream without checking its age.
- Evaluate deliberate filtering/decimation or selection of the newest valid matched pair for each
  processing interval. A later 60 Hz filter path must not treat one barometer measurement as several
  independent measurements. Record the final choice and its measured latency/noise here.
- Track FIFO overruns, bounded-drain truncation, stale pairs, invalid vectors, bus errors, and
  SFLP recovery.

### Startup while moving and gyro calibration

Startup and wake must work while Leaf is already flying. A **3-10 second warm-up** before useful
output is acceptable. A stationary period is not a prerequisite, and ten seconds elapsed is not
proof that the gravity estimate is valid. The datasheet's typical calibration/stabilization times
are context, not a guarantee for an airborne start.

- Discard sensor/fusion startup transients. Require fresh, finite, plausibly sized vectors,
  appropriate pairing, and a sequence of usable samples before enabling climb output.
- Readiness must not depend on motionlessness, a fixed mounting angle, or acceleration remaining
  close to 1 g. Do not carry the old stillness-like startup gate into the new path.
- Show startup progress and unavailable climb output during warm-up; keep vario audio muted until
  measurements and the filter are ready.
- Initialize the Kalman filter from fresh barometric altitude and valid acceleration. Its initial
  zero velocity is an initialization assumption, not a measurement that Leaf is stationary;
  verify convergence during an ongoing climb or sink.
- Test cold starts and wake-ups during straight flight, climb/sink, rotation, and sustained turns.

ST documents gyro-bias estimation while stationary and a procedure to seed SFLP with a previously
measured bias when stillness cannot be guaranteed (AN5922, Section 6.5.1). The proposed initial
design captures trusted gyro bias during stationary commissioning, stores it with a calibration
version, and restores it before fusion warm-up. Use ST's supported API/sequence for the pinned
driver version, including required reset and unit conversions.

Capture that calibration **after the factory nonvolatile reset and chip self-test**. Restore it
on ordinary cold boot; retain a trusted copy in RAM for charging-mode wake where suitable. Avoid
frequent flash writes. Define validity checks, sensor/configuration compatibility, and deliberate
invalidation or migration of stored records. Test OTA retention, recommissioning, temperature
changes, and missing/corrupt calibration after a factory reset. Missing calibration must produce
defined behavior, not an endless request to remain still.

Saved gyro bias is separate from accelerometer correction and Leaf's gravity-magnitude estimator.
It does not by itself prove gravity convergence during sustained acceleration. If moving-start
qualification fails, revisit calibration or fallback behavior before claiming the requirement met.

### Fault handling and output validity

- Bound initialization retries and verify chip identity, reset completion, and configuration.
  Missing/wrong hardware must yield a useful diagnostic or commissioning failure.
- Reject invalid, stale, saturated, or badly paired samples. Handle FIFO overflow, partial reads,
  repeated bus failures, and unexpected chip resets without publishing plausible stale values.
- Reset/requalify fusion and filter state after a significant interruption. Count faults and recovery.
- Propagate invalid climb data through display, audio, logging, and other consumers. Preserve
  available barometric altitude without advertising an old fused climb rate as current.

The proposed initial behavior is a visible fault and muted vario audio during recovery. A
barometer-only climb fallback is an **open product decision** and requires an explicit, tested
filter path. Passing zero acceleration into the existing filter is not automatically such a
fallback. This supersedes the earlier tentative instruction to continue barometric updates with
an intentional zero correction.

### Accuracy and dynamic limitations

The datasheet specifies SFLP attitude accuracy, not derived vertical-acceleration accuracy:

| Condition | Pitch error | Roll error |
| --- | ---: | ---: |
| Static | 1.5 degrees | 1.5 degrees |
| Low dynamic | 0.5 degrees | 0.5 degrees |
| High dynamic | 1.6 degrees | 1.2 degrees |

The accelerometer specification includes approximately +/-12 mg zero-g offset and
+/-0.07 mg/degree C offset drift. A 12 mg vertical error is about 0.118 m/s^2 and can create
a persistent climb-rate bias. Whether additional correction is necessary is a validation question;
the SFLP gravity quantization of 0.061 mg/LSB is unlikely to dominate.

As a planning check, a numerical reproduction of Leaf's current Kalman equations at 20 Hz, with
perfectly constant barometric altitude and the current 0.1 m position / 0.3 m/s^2 acceleration
standard-deviation settings, converged to approximately **0.093 m/s false climb for a constant
12 mg acceleration residual**. This was a 300-second numerical calculation, not a new-board
measurement. The filter has no explicit accelerometer-bias state, so barometric correction alone
does not guarantee zero steady-state velocity error from biased acceleration.

No six-axis system can perfectly distinguish gravity from sustained linear acceleration. During
a coordinated turn, the accelerometer measures gravity plus centripetal acceleration. Gyro
integration helps through short dynamics, but prolonged acceleration can pull the estimated
gravity direction. This is particularly relevant during sustained thermalling, so the barometer
must correct long-term IMU errors.

For small gravity-direction error `theta`, horizontal-to-vertical leakage is approximately
`horizontal_acceleration * sin(theta)`. Evaluate this with real turn accelerations, not only
stationary tests.

## Driver and Message Implementation Work

- Pin ST's platform-independent LSM6DSV C driver, retain its license, and add a thin adapter to
  Leaf's existing I2C bus. Keep chip register handling separate from instrument/filter logic.
- Add a small compile-time-selected interface for initialization, polling, sleep, wake, bus
  publishing, and diagnostics. Avoid a broad sensor-framework rewrite.
- Replace direct ICM singleton dependencies in startup, task scheduling, power handling, debug
  displays, and message injection/replay controls with the selected hardware interface.
- Check identity at `0x6A`, apply reset and configuration, and verify critical settings. Avoid
  the unbounded initialization retry loop used by the current ICM driver.
- Poll the unread FIFO entry count and drain available entries on each service pass. Avoid waiting
  for a large watermark that would unnecessarily delay low-rate updates.
- Select supported accelerometer, gyroscope, fusion, and batching rates explicitly. Do not assume
  the current ICM rates or packet structure transfer directly.
- Parse FIFO tags and preserve sample timing and the association between orientation and acceleration.
  Count all enabled streams when calculating FIFO production and required service capacity.
- Allow for delayed service during logging, display work, or other bus activity. Track overruns,
  invalid data, read failures, and recovery, with a bounded drain strategy that can catch up.
- Initially retain approximately 20 Hz host polling, then measure latency and backlog before fixing
  the final rate. This interval adds up to approximately 50 ms of notification delay, plus scheduling
  delays; it does not require the sensor itself to sample at 20 Hz.
- Extend `MotionUpdate` with an optional gravity vector and explicit validity/timing semantics.
  Keep raw acceleration available and preserve the existing ICM orientation fields. Do not require
  an orientation quaternion for the v3.2.8 gravity-vector path or fabricate one for compatibility.
- Define units and sensor/board coordinate conventions at that boundary. Preserve total
  acceleration independently for flight logging, g-load metrics, and automatic flight-stop logic.
- Update bus logging, `src/vario/dispatch/message_injector.h` / `.cpp`, simulator recordings, and
  replay tooling together. The simulator and on-device UDP injection share the parser. Keep old
  recordings readable and retain a way to exercise both IMU processing paths.
- Add diagnostics for gravity XYZ/magnitude, gravity age, pairing delta, projected acceleration,
  applied correction (if any), gyro bias, temperature, FIFO levels/overruns, invalid vectors,
  calibration status, startup state, and SFLP restarts. Preserve useful legacy diagnostics.

## Kalman and climb-rate integration

Retain `KalmanFilterPA` initially. Feed it positive-up vertical acceleration in m/s^2 and
barometric altitude in meters. Start from the existing noise settings, then characterize residual
noise, latency, and motion response before changing the weights. Do not infer appropriate filter
noise solely from the component's static accuracy specifications.

- Establish a common monotonic timebase for IMU and pressure data. Use acquisition time rather
  than assigning every queued sample the host time at which it was read.
- Bound sample age and the time difference between acceleration, gravity, and barometer data.
- Process a barometric observation once; draining several IMU samples must not count the same
  altitude repeatedly as independent evidence.
- Handle duplicate/out-of-order samples, clock rollover, long gaps, and delayed processing.
- Add an explicit filter reset/invalidation path for sleep, wake, sensor reset, and long data gaps.
  The current filter reinitializes when an update gap exceeds one second, but `IMU::wake()` does
  not explicitly clear it or all cached validity flags.
- Ensure stale IMU velocity cannot remain marked valid. Clear or requalify the downstream climb
  averages/display history as needed, and stop stale climb values from driving audio or navigation.
- Retain barometric altitude as the long-term reference and check climb response during an
  airborne start; startup must not assume the true climb rate is zero.

Current integration points are `src/vario/instruments/imu.cpp`, `src/vario/math/kalman.cpp`, and
`src/vario/instruments/baro.cpp`. The last currently consumes the IMU filter's velocity and drives
climb filtering and vario audio, so invalidation must extend beyond the hardware driver.

## Power lifecycle

The current ICM wrapper exposes initialization and polling without chip sleep/wake operations.
`Power::sleepPeripherals()` sleeps other peripherals, while `Power::wakePeripherals()` calls the
IMU instrument's partial reset. Add deliberate hardware and instrument transitions for v3.2.8.

| Leaf state or transition | Planned IMU behavior |
| --- | --- |
| Cold boot into operating mode | Initialize sensors/fusion, discard transients, obtain fresh pressure and motion, then qualify climb output within the moving-start target |
| Cold boot directly into USB charging mode | Keep the IMU powered down; avoid unnecessary fusion startup |
| Normal operation | Keep sensors and fusion running continuously, including between normal host polling cycles and during brief ESP32 idle/sleep intervals |
| Shutdown into USB charging mode | Stop publishing, invalidate motion/filter output, disable fusion/FIFO activity, and power down accelerometer and gyro |
| Battery-powered shutdown | Perform the same orderly stop before releasing the power latch |
| Wake from USB charging mode | Restore configuration and trusted calibration, flush old FIFO data, restart fusion, reset filter state, and requalify output while moving |
| Unexpected sensor reset | Detect interruption, recover configuration, and repeat readiness checks |
| Self-test entry/exit | Suspend normal publishing, isolate test samples, restore configuration, flush FIFO, and restart fusion/filter readiness |

Prefer power-down for long charging/off periods. Consider gyro sleep for short interruptions only
if measured wake latency justifies another state. Do not suspend the IMU on every short host idle
period or introduce motion-triggered sleep during flight. Disable unwanted interrupt sources while
charging so the new device does not prevent host sleep. Verify repeated transitions and current
consumption on both USB and battery.

## Commissioning and self-tests

The current `SelfTest::testIMU()` waits for valid acceleration and velocity, then accepts total
acceleration between 0.8 and 1.2 g. That broad check can pass a sensor with enough residual error
to produce false climb. Update v3.2.8 testing while retaining the old-board path.

1. Verify chip identity and fresh data with bounded timeouts and specific failure reasons.
2. Run ST's accelerometer and gyroscope stimulation self-tests using the required ranges, settling
   delays, and limits from the pinned driver example. Suspend normal motion publishing so test
   acceleration cannot reach the vario, logs, or flight logic.
3. Restore normal settings and flush test data. Reinitialize fusion and filter readiness even if a
   test fails or is cancelled, while retaining the failure result.
4. After the commissioning nonvolatile reset and chip self-test, capture trusted gyro bias during
   a stationary window and persist the versioned calibration. Ordinary boot does not require
   repeating this stationary step.
5. Measure acceleration magnitude, gravity magnitude, projected residual mean/noise, sample rate,
   temperature, and settling behavior. Establish production thresholds using several new boards.
6. Retain the interactive lift/lower vario test, with upward/downward sign and audible response
   checks. Retain speaker and other existing commissioning tests.

Use six-face orientation testing during engineering qualification. Add it to routine production
calibration only if the measured accelerometer errors justify the additional operator work.
The unused MMC5983MA must not be a required test or a reason to fail initial v3.2.8 commissioning.

Record chip identity, calibration version/status, numeric results, and useful fault reasons in
the existing `/self_test_N.txt` output. Verify the highest-numbered result can still be retrieved
through `GET /self-test/details` and persisted in the factory session's `test_results`.
Keep existing task names and success/failure/cancellation semantics compatible with the setup
flow. If checklist text changes, keep `setup_checklist.html` and `setup_session.html` in sync and
confirm terminal states do not accidentally auto-start the next task.

Replace obsolete gravity-learning readiness labels/counters in startup progress and debug UI with
the actual v3.2.8 sensor/fusion/calibration state. Keep diagnostics meaningful for older boards.

Reference: ST's [LSM6DSV self-test example](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lsm6dsv_STdC/examples/lsm6dsv_self_test.c).

## INT1 and future interrupt work

The completed v3.2.8 board connects **IMU INT1 to IO expander P14 (`TCA_P14`)**. INT2 is unused.
Initial support retains polling with interrupt sources disabled. Tap/double-tap gestures are a
future use for the existing INT1 path; gesture implementation is outside this initial migration.

The pins are not fully interchangeable. INT1 provides the best coverage for a single connection.

| Interrupt source | Routing |
| --- | --- |
| Normal accelerometer and gyroscope data-ready | Either pin |
| FIFO watermark, full, overrun, and batching counter | Either pin |
| Wake-up, free-fall, taps, orientation, activity/inactivity | Either pin |
| Step detection, tilt, significant motion, FSM events | Either pin |
| Sensor-hub transaction complete | INT1 only |
| Temperature data-ready, EIS gyroscope data-ready, timestamp overflow warning, embedded-function end-of-operation | INT2 natively; movable to INT1 |
| OIS data-ready | INT2 only; no documented INT1 remapping |

For the four movable signals, set `CTRL4.INT2_ON_INT1` (bit 4 at `0x13`) and their corresponding
INT2 source enables. They are ORed onto INT1 and removed from INT2. This is not a universal
remapping switch. OIS data-ready overrides other INT2 output settings.
See the [datasheet, sections 9.11–9.17, 9.62–9.63, 9.73, and 13.7–13.10](https://www.st.com/resource/en/datasheet/lsm6dsv.pdf).

INT2 also supports external input functions: sampling synchronization, DEN tagging, and sensor-hub
triggering. Revisit its connection if these become requirements; they are not part of initial polling.
See the [datasheet, sections 6.6, 9.51, and 15.3](https://www.st.com/resource/en/datasheet/lsm6dsv.pdf).

## When Interrupts Would Help

Potential reasons to use the connected INT1 later include waking the host on motion, reducing
unnecessary polling, or responding to FIFO status sooner. Two interrupt connections would additionally
allow routine data notifications and urgent events to use separate host handling and wake policies.

With one line, firmware must inspect status and service every pending source. Because enabled sources
are ORed, one asserted source can hide another source's edge. Check clearing and latching behavior
before mixing event types. The embedded-function end-of-operation signal is intended for execution
timing/debug and should not be treated as an ordinary motion event.
See [AN5922, section 5.1](https://www.st.com/resource/en/application_note/an5922-lsm6dsv-6axis-imu-with-embedded-sensor-fusion-i3c-oiseis-for-smart-applications-stmicroelectronics.pdf).

The v3.2.8 connection uses the IO-expander path rather than a direct MCU GPIO, consistent with
the existing v3.2.6/v3.2.7 arrangement. Before implementing gestures or wake-on-motion, verify the
complete expander interrupt, source-clearing/latching, and ESP32 sleep-wake path. Connecting INT1
alone does not establish that wake-on-motion is implemented. Preserve other users of the shared
expander interrupt when servicing IMU events.

## Separate Magnetometer and Fusion

The LSM6DSV is a six-axis device without an integrated magnetometer. Its embedded SFLP fusion uses
acceleration and angular rate and produces a game rotation quaternion, gravity, and gyro bias through
the FIFO. It does not directly replace Leaf's existing magnetically referenced Quat9 output.
See [AN5922, section 9](https://www.st.com/resource/en/application_note/an5922-lsm6dsv-6axis-imu-with-embedded-sensor-fusion-i3c-oiseis-for-smart-applications-stmicroelectronics.pdf).

The fitted magnetometer is **MMC5983MA at `0x30`**, with INT unused. Selection is no longer open.
Do not add a magnetic data stream, compass calibration, heading fusion, or a required magnetometer
self-test for initial v3.2.8 support. Confirm that it remains inactive on cold boot and across
ESP32-only resets; add only minimal power-management handling if measurements show it is needed.

Leaf's old ICM driver requests Quat9, so the integrated magnetometer participates indirectly even
though Leaf does not expose a compass reading. The replacement uses explicitly six-axis SFLP;
its yaw drift does not require magnetic correction for the vertical projection.

If magnetic heading is added later, separately design the access path, host-side fusion,
CPU/flash budget, sensor-axis mapping, hard/soft-iron calibration and persistence, and behavior
under magnetic disturbance. Attaching a magnetometer to the sensor hub does not by itself turn
SFLP into nine-axis fusion. These future choices do not block the current migration.

## Speaker replacement

The fitted speaker is **CPT-9019A-SMT-TR**. The hardware owner confirmed that no additional
speaker firmware changes are needed, so retain tone generation, volume control, and existing
sound settings. It is an externally driven piezo transducer with a 4 kHz rated frequency; this
does not mean Leaf's tone range should be changed to 4 kHz.

Include ordinary audio regression checks during board qualification: audible tone range, all
volume levels, actual terminal drive voltage, and silence after shutdown. Check whether loud
tones introduce mechanical vibration or electrical interference into IMU/barometer measurements.
Make additional audio changes only if those tests identify a concrete problem.

Reference: [CPT-9019A-SMT-TR datasheet](https://www.sameskydevices.com/product/resource/cpt-9019a-smt-tr.pdf).

## Verification Backlog

### Automated processing and recovery checks

- Exercise projection in multiple orientations, upside-down mounting, and known upward/downward
  acceleration. A common sensor-to-board rotation must not change the scalar result.
- Reject zero/invalid gravity vectors, non-finite acceleration, saturation, stale data, and
  deliberately misaligned FIFO records.
- Exercise partial FIFO reads, overruns, delayed polling, bounded draining, and sensor-reset recovery.
- Cover duplicate/out-of-order timestamps, clock rollover, long gaps, and reuse of barometer samples.
- Verify startup while moving, filter invalidation/reset, and repeated charging-mode sleep/wake.
- Replay old ICM recordings and new gravity-vector recordings through the shared injection path.

### Bench bring-up

- Verify identity, reset, SFLP enable, ODRs, full scales, and conversion factors.
- Verify gravity direction/sign in all six stationary orientations.
- Verify stationary `accel_up_g` is near zero in all six orientations, first without Leaf gravity
  correction and then with any evidence-supported calibration being evaluated.
- Verify upward movement is positive and downward movement is negative.
- Measure SFLP startup, output cadence, gravity magnitude, latency, and phase versus acceleration.
- Exercise delayed FIFO service, overrun recovery, IMU reset, and stale-pair rejection.
- Compare direct gravity projection with SFLP-quaternion rotation as a development cross-check.
- Repeat on several boards and over relevant temperatures. Log gravity magnitude, acceleration
  residual mean/noise, gyro bias, and current consumption.
- Exercise USB-only boot, battery boot, USB shutdown, low-battery shutdown, repeated wake cycles,
  missing/corrupt calibration, and factory reset/recommissioning.
- Check loud speaker operation and other bus activity for interference and timing delays.

### Vario and simulator integration

- Preserve ICM gravity learning. Evaluate whether v3.2.8 needs any correction using the criteria
  below and the [IMU gravity performance backlog](vario-imu-gravity-performance.md).
- Add recordings containing acceleration and gravity vectors to exercise the real path in `sim/`.
- Confirm invalid IMU data cannot inject persistent acceleration into the Kalman filter.
- Confirm stationary filtered climb settles near zero across orientations and temperatures.
- Compare noise, response, and settling against the ICM-20948 implementation.
- Verify startup progress, diagnostic pages/logs, unavailable climb indications, audio, automatic
  flight-stop behavior, and navigation consumers do not retain stale motion/climb data.

### Flight validation

- Log straight flight, pitch/roll transitions, turbulence, turn entry/exit, and sustained
  thermalling at multiple bank angles.
- Correlate IMU acceleration, barometric response, and the current ICM implementation.
- Look for false impulses from sample misalignment and long-duration bias in coordinated turns.
- Include cold starts and wake-ups during flight, with and without saved gyro calibration, and
  during sustained banked turns. Allow the agreed 3-10 second warm-up without requiring stillness.
- Tune ODR, processing rate, validity gates, any required calibration, and Kalman noise from evidence
  rather than carrying ICM settings forward unchanged.
- Measure power and end-to-end latency before deciding whether to enable INT1.

### Manufacturing, builds, and release compatibility

- Update IMU commissioning as described above; do not require the unused magnetometer to pass.
- Decide from board measurements whether additional accelerometer calibration or manufacturing
  limits are needed beyond the proposed stationary gyro-bias capture.
- Verify test pass/failure/cancellation paths and persisted results through the factory interface.
- Keep the ICM-20948 driver selectable and check older-board commissioning for regressions.
- Run the formatter and simulator checks, then build both v3.2.8 environments and the existing
  ICM variants covered by CI. Include `leaf_3_2_6_release` as a legacy regression build.
- Follow `AGENTS.md` for hardware verification when firmware implementation begins: detect the
  connected board/port, confirm the v3.2.8 target, and flash the matching firmware to both OTA slots
  using the confirmed partition layout. Do not flash a new-board image to an older board.
- Verify OTA/release selection returns the hardware-specific binary and that factory selection
  works with both local application builds and release assets. Non-application images still come
  from a suitable local PlatformIO build.
- Record actual build, simulator, commissioning, and hardware results here when performed.
  See the implementation record for completed checks; unchecked physical items remain open.

## Proposed acceptance criteria

These are proposed engineering targets; characterize environmental/barometer noise and agree any
necessary adjustment before treating them as production limits.

- After settling, stationary mean climb within **+/-0.05 m/s over a 60-second window** in each
  orientation, across several boards. Record residual noise separately from the mean.
- Positive-up acceleration and correct climb/sink response, with no orientation-dependent bias
  large enough to violate the stationary target.
- Useful climb output after **3-10 seconds** when started or awakened while moving/flying, including
  ongoing climb/sink. No stationary-start requirement and no automatic declaration of validity
  solely because a timeout elapsed.
- Measured motion response/latency comparable to or better than the existing path, with no false
  impulses during rotation or persistent bias during representative sustained turns.
- Invalid, absent, stale, or reset sensor data cannot drive a persistent false climb tone or remain
  silently marked valid. Fault indication and recovery behave as specified.
- Sleep/shutdown stop unnecessary IMU activity; wake does not replay old FIFO/filter state.
- Commissioning detects relevant sensor faults, saves calibration/results correctly, and remains
  operable without magnetometer data.
- Old hardware, old recordings, release selection, and OTA behavior retain their required behavior.

Do not remove the legacy ICM estimator globally when these targets pass on v3.2.8. Record the
new-board correction choice and the measurements supporting it separately.

## Implementation sequence

1. Add the v3.2.8 variant, pinned ST driver, selected hardware interface, and bounded bring-up.
2. Add paired acceleration/gravity messages, diagnostic capture, and direct vertical projection.
3. Integrate timestamps, validity, Kalman reset, and downstream climb/audio handling.
4. Implement moving startup, calibration persistence, and the full power lifecycle.
5. Update commissioning, factory result retrieval, debug/startup UI, and replay/simulator support.
6. Run automated, bench, temperature, power, and moving-start checks; decide whether extra
   accelerometer correction is necessary and tune from the measurements.
7. Complete flight validation and legacy/build/release regression checks before considering support ready.

## Expected code and documentation touchpoints

| Area | Files or directories |
| --- | --- |
| Hardware variant and build matrix | `src/variants/leaf_3_2_8/`, `platformio.ini`, `.github/workflows/build.yaml` |
| Selected driver and bus configuration | `src/vario/hardware/`, including new LSM6DSV wrapper and the ICM compatibility path |
| Startup, polling, and power | `src/vario/main.cpp`, `src/vario/taskman.cpp`, `src/vario/power.cpp` |
| Projection, filter, and climb | `src/vario/instruments/imu.*`, `src/vario/math/kalman.*`, `src/vario/instruments/baro.*` |
| Motion messages and replay | `src/vario/dispatch/message_types.h`, `message_injector.*`, `src/vario/logging/buslog.*` |
| Calibration persistence | Existing settings/storage facilities, with a versioned sensor-calibration record |
| Commissioning | `src/vario/diagnostics/self_test/`, relevant `factory_interface/src/factory_interface/` integration |
| Startup/debug and diagnostics | Display fields, debug pages, and vario diagnostic logging |
| Simulator | `sim/device/sensors.cpp`, `sim/Makefile`, recordings, replay tools, and focused tests |
| Versioning and firmware selection | Verify existing `src/scripts/` and factory/OTA discovery; change only where required |
| Hardware and design record | `vario/hardware/hardware_changes.md` and this backlog |

## Implementation and bring-up record (2026-10-20)

Branch: `codex/hardware-v3.2.8`, based on `origin/main` at `82a74de7`.

Implemented the v3.2.8 dev/release environments and CI entry, compile-time motion-driver
selection, and vendored LSM6DSV register driver v5.0.1. The wrapper uses 60 Hz accel/gyro/SFLP,
±4 g and ±2000 dps, and batches acceleration and gravity only. Interrupt routes and sensor hub
remain disabled; no MMC5983MA driver is initialized. ICM hardware retains its previous driver
and Leaf gravity estimator.

FIFO words are associated by the two-bit time-slot counter, with incomplete groups rejected.
The newest complete pair is published once per existing 20 Hz host poll. This first implementation
uses host receive time, **not reconstructed sensor timestamps**; it rejects overrun, more than
16 queued words, and polling gaps over 200 ms. It does not replay a FIFO burst against one
pressure observation. Measuring phase/latency and deciding whether sensor timestamp reconstruction
is needed remain bench/flight validation work.

The new instrument path projects `(a - g)` onto normalized SFLP gravity. It does not learn Leaf's
scalar gravity correction. An experimental third Kalman state estimates residual vertical
acceleration bias using barometric altitude (see comparison below). Startup/wake/reset use a
three-second driver warm-up followed by
20 fresh Kalman updates. Pressure and motion expire after 250 ms, reset invalidates the filter,
and invalid climb clears downstream averages and the vario tone. New-hardware climb displays
show `--` while invalid. Each pressure sample is used at most once; elapsed Kalman time handles
the millisecond clock wrapping. Moving initialization uses a 5 m/s initial velocity standard
deviation instead of a confident zero-velocity prior. Legacy filter initialization is preserved.

Commissioning now runs asynchronous accelerometer and gyro electrical self-tests using ST's
response limits, then collects 100 stationary gyro/acceleration samples for a versioned gyro-bias
record in NVS namespace `imu328`. A factory NVS erase clears that record. Normal boot only restores
it; it never runs stationary calibration. Missing calibration uses a zero seed and an explicit
diagnostic status. Electrical tests always disable stimulus and restore operational configuration
on completion/failure; shutdown cancels the test. Structured results use the existing
`self_test_N.txt` and `/self-test/details` path. Production calibration thresholds still need
validation across boards and temperature.

Recording format remains compatible with old motion lines, with optional
`,V,gx,gy,gz,reset` fields for SFLP data (lowercase `v` means no gravity). Simulator tests cover
already-climbing startup, IMU loss, pressure loss, reset, invalid gravity, duplicate motion
timestamps, recovery, and changed mounting. `sim/tests/imu_replay.py` creates the fixtures;
`sim/tests/gravity_projection.cpp` checks sign, six faces, arbitrary tilt, horizontal acceleration,
freefall, clipping, and nonfinite input.

Initial verification:

- v3.2.8 release and dev builds passed; v3.2.6 release regression build passed.
- Existing simulator first-boot test passed. Gravity projection and full estimator replay checks
  passed. The recovery test exposed overshoot from the old zero-velocity/zero-uncertainty prior;
  the new initialization recovered approximately 1.99–2.00 m/s for a 2.00 m/s input.
- The connected board's flash was blank. Bootloader, partition table and both OTA slots were
  installed and verified. For a blank board, the **bootloader must be written with DIO mode**:
  forcing QIO caused a ROM-loader watchdog reset (`ets_loader.c 78`), resolved by correcting only
  the bootloader header to DIO. The existing factory interface already translates QIO to DIO
  for upload. Application-only flashing leaves that bootloader untouched.
- The user confirmed startup, liability screen and a normal vario page. Detailed physical IMU,
  commissioning, power/current, temperature and flight validation are still in progress; a
  successful build or synthetic replay is not evidence that those measurements have passed.

### Correction comparison and first commissioning measurements

The first v3.2.8 board produced approximately 0.988–0.990 g resting acceleration while SFLP
gravity remained approximately 1 g. Direct projection therefore left about −0.01 to −0.012 g
vertical acceleration and roughly −0.06 to −0.18 m/s apparent sink in sampled diagnostics.
This is evidence for a residual correction, not evidence by itself for a particular correction
model. One orientation on one board cannot distinguish axis offset, scale error, temperature
effects, or all attitude errors.

On-device commissioning passed all six electrical responses: accelerometer 265.374, 264.691,
175.875 mg; gyro 399.91, 415.8, 411.46 dps. The stationary check passed and saved gyro bias
−0.4452, +0.2310, −0.1967 dps. Reconfiguration restored the bias with zero I2C errors.
The subsequent IMU check passed with acceleration 0.990289 g, gravity 1.00044 g, residual
−0.0101508 g and climb −0.119822 m/s. Thus gyro commissioning did **not** remove the
accelerometer residual. These electrical/communication checks do not certify climb accuracy.
FIFO diagnostics showed approximately 59 matched pairs/s and zero counter mismatches.

The overall self-test is **not** a pass: the separate POWER check reported USB present,
charging false, 79% / 3921 mV and "Inconsistent power state". Interactive tests were still
pending at capture end. Preserve this result for separate power/commissioning investigation.

At the user's suggestion, compared the existing gravity estimate against the experimental
Kalman bias state using the actual two instrument paths in the simulator. The existing scalar
filter absorbs 90% of accepted vertical-force changes over five seconds, with its existing
gates/slew limits. It removes a resting offset quickly, but can also learn genuine sustained
vertical acceleration. The alternative state is `[altitude, climb, vertical acceleration bias]`:
prediction uses measured vertical acceleration minus bias; barometric altitude updates all
three states. Initial bias standard deviation is 0.25 m/s², bias random walk is 0.005 m/s²/√s,
and initial velocity standard deviation remains 5 m/s. These are provisional tuning values.
The two correction mechanisms are not enabled together.

`sim/tests/imu_filter_comparison.py` generates a consistent 2 m/s moving start, twenty seconds
of +0.3 m/s² vertical acceleration, constant climb, −0.4 m/s² deceleration to rest, and a
projected bias step from −0.012 g to +0.008 g. Pressure is quantized to 1 Pa. Results from
2026-10-20 (synthetic, ideal attitude and synchronized sensors, no added random noise):

| Window | Existing gravity filter | Kalman bias state |
| --- | ---: | ---: |
| Moving startup, 6–10 s, RMS climb error | 0.0100 m/s | 0.0048 m/s |
| Sustained acceleration, 25–39 s, RMS climb error | 0.2333 m/s | 0.0067 m/s |
| Steady climb after acceleration, 43–49 s, RMS error | 0.0311 m/s | 0.0049 m/s |
| Resting, 85–100 s, mean climb error | +0.00006 m/s | −0.00016 m/s |
| 30–45 s after bias step, mean climb error | approximately zero | +0.0106 m/s |

This supports bench evaluation of the bias state; it does not establish flight readiness or
prove superiority under pressure disturbances or changing attitude. The existing filter reacts
faster to a changed offset. The comparison's old path retains its historical velocity prior;
the new path uses the moving-start prior. Neither scalar correction fixes an incorrect gravity
direction. Pressure lag, airflow disturbances, temperature changes, sustained turns and rotation
of a per-axis bias into the vertical still require recorded-data and physical validation.

The user proposed using a successful bias estimator for the old IMU too. That is a sensible
follow-up: retain ICM quaternion-based vertical projection, replace only its scalar correction,
then validate startup, drift, maneuvers and fault recovery with real ICM recordings/hardware.
For this hardware-support branch, the ICM continues using its existing correction. Do not
remove it solely on the basis of the new-board synthetic comparison.

The experimental bias build was flashed to both application slots and the saved gyro calibration
was restored after restart. In the first resting orientation (gravity approximately
`[0.187, 0.018, 0.982] g`), twelve consecutive five-second diagnostic windows contained 1201
valid climb updates over approximately 60.05 seconds, beginning about 13 seconds after wake:
mean climb **−0.00035 m/s**, RMS climb **0.03377 m/s**. The estimated vertical bias settled
near **−0.095 m/s²**. Statistics accumulate every published valid update; these are not means
of the occasional point readings. No FIFO counter mismatches or IMU recovery events were
observed in that interval. This meets the resting mean target in this orientation only.
It does not establish six-face, moving-start, temperature or flight acceptance.

Two timed diagnostic captures closed/reopened the ESP32 USB Serial/JTAG port with pySerial's
default RTS assertion, coinciding with a return to the charging screen and a restarted device
clock. This is consistent with the native USB reset-control sequence, not yet evidence of an
IMU crash. Subsequent capture explicitly sets `rts=False` **before** opening the port and keeps
one connection open. See the USB Serial/JTAG reset-control states in the
[ESP32-S3 technical reference manual](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf).
Keep this setting in bench-capture scripts and investigate any restart that occurs independently
of host control-line changes. The interrupted upright capture is excluded from resting statistics.
With RTS inactive, a subsequent close/reopen check retained the running device clock
(approximately 299–309 seconds), valid climb and increasing FIFO pair count without a restart.
This supports the host-control explanation for the two interrupted captures.

After the bias estimator and diagnostic averages were added, formatting, v3.2.8 dev/release
builds, v3.2.6 release regression, gravity-projection tests, biased/unbiased SFLP fault/recovery
replays, the filter comparison and the legacy simulator first-boot check passed.
The diagnostic application remains installed for bench testing; both OTA slots were verified.
Its SHA-256 is `003a4390e798396f6acd708eaef5b65fc4ba2beddd894104e0f8a26ef65089ac`.
The release image was built but has not replaced this diagnostic application.

With the capture connection held open and RTS inactive, the repeated upright test remained
operational. Gravity was approximately `[-0.192, 0.922, 0.338] g`. A separate 60-second window
beginning about 13 seconds after wake contained 1200 valid updates: mean climb **−0.00318 m/s**,
RMS **0.03596 m/s**, with bias around −0.03 to −0.04 m/s² and no counter mismatches.
This meets the resting mean target in the second tested orientation. Because the connection
restart interrupted the original rotation test, this second window validates a fresh wake in
that orientation, not uninterrupted in-flight adaptation to a 90-degree rotation.

Outstanding physical acceptance includes six faces, continuous orientation changes/turns,
moving cold start and wake, temperature drift, sensor/pressure latency, low-power current and
fault injection. The unrelated POWER self-test failure also needs resolution before calling
commissioning complete. No flight-readiness claim or global ICM-estimator replacement follows
from these two resting windows.

## Resume from this checkpoint (2026-10-20)

This is an implemented, bench-tested development checkpoint, not a flight-qualified release.
The implementation record above supersedes earlier planning alternatives where they differ.
In particular, acceleration/gravity are the only FIFO streams enabled, host receive timestamps
are used, and the new hardware currently uses the experimental Kalman bias correction.

### Device and local artifacts

- The tested ESP32-S3 board is MAC `7c:4f:ad:3b:ac:b0`. Both OTA slots contain the v3.2.8
  **dev** application identified by the SHA-256 above. Its gyro calibration is saved in NVS.
  Do not erase NVS merely to resume ordinary testing.
- Auto-detect the Espressif USB port on each connection. The dev application uses native USB
  Serial/JTAG; the release application's USB availability changes with the power state.
  For diagnostic capture, construct the serial object without opening it, set `rts=False`
  and `dtr=True`, then open it. Keep the connection open throughout a measurement.
- Local, **gitignored** artifacts are under `.pio/imu-validation/`: the tested dev binary/ELF,
  release binary, and two resting-statistics summaries. Captures are `.pio/imu-board-test.log`,
  `.pio/imu-bias-board-test.log`, `.pio/imu-bias-upright.log`, and
  `.pio/imu-serial-reopen-check.log`. Build output is in `.pio/imu-{dev,release,legacy}-build.log`.
  These files are not available from a fresh clone; the measured results and image hash are
  retained in this document. Preserve the ELF if investigating this particular flashed image.
- Application-only flashing writes both `0x10000` and `0x340000`. Preserve the working DIO
  bootloader. For a new blank board, follow the existing factory DIO bootloader-upload behavior.
- At this earlier checkpoint the full commissioning run was not completed: the POWER check
  failed and interactive checks remained pending. The later complete test commissioning below
  supersedes that incomplete result; it does not explain the earlier POWER failure.

### Next work, in order

1. Capture controlled vertical movement, continuous rotation, and starts/wakes while moving.
   Confirm correct climb sign, response latency and useful data within 3–10 seconds. Keep
   barometric altitude, raw acceleration, gravity, correction and climb together in the record.
   Synthetic moving-start tests passed; physical moving-start acceptance is still open.
2. Expand the resting checks to all six faces, multiple boards and temperature changes. Test
   how quickly the bias state follows orientation-dependent error and whether saved gyro bias
   remains useful across temperature. Decide whether any per-axis accelerometer calibration is
   needed; do not enable the legacy gravity learner alongside the bias estimator.
3. Measure IMU/barometer phase and latency under realistic scheduler load. Decide whether the
   simple newest-pair/host-time approach is sufficient before adding sensor timestamp handling.
   Finalize rates, validity thresholds and Kalman noise from these measurements.
4. Exercise hardware communication failures, stale data, backlog and saturation; review recovery
   limits and fault reporting. Verify repeated off/charging/wake cycles and actual IMU power-down
   current. Software recovery and power-down exist, but physical fault/current tests are pending.
5. The complete commissioning sequence passed once in the later test session below. Check
   missing/corrupt calibration, interrupted tests, recommissioning and update retention.
   Investigate the earlier POWER failure rather than weakening its check; establish production
   thresholds using more than one board.
6. Collect representative flight recordings, including sustained turns and moving startup.
   Compare response and false indications against the existing implementation. Then review the
   firmware changes, decide which temporary serial diagnostics to retain, and validate the normal
   release USB/charging and update paths on hardware before release.

Magnetometer support, tap gestures and migrating the ICM to the bias estimator are follow-ups,
not prerequisites for the agreed initial v3.2.8 scope. Barometer-only fallback remains a separate
product decision; the current implementation invalidates fused climb and mutes the vario.

### Reproducing the automated checks

Run the repository formatter and the `leaf_3_2_8_dev`, `leaf_3_2_8_release` and
`leaf_3_2_6_release` PlatformIO builds according to `AGENTS.md`. Build firmware environments
sequentially because they share the generated `src/vario/leaf_version.h`; do not commit generated
version values. The last completed builds and checks passed; no firmware code changed afterward
while preparing this handoff.

For the new-board simulator, build in the repository's documented Docker environment with
`make -C sim BUILD=/leaf/sim/build328 VARIANT=leaf_3_2_8 -j8`. On Windows use the PlatformIO
environment's Python if the system `python` command is the Microsoft Store alias.

- Generate replay fixtures with `python sim/tests/imu_replay.py` and
  `python sim/tests/imu_replay.py -0.012`. Run `sim/build328/leafsim` against each corresponding
  `sim/build/imu-sflp.log` or `sim/build/imu-sflp-biased.log` and matching `.txt` using
  `--port 0 --speed 0 --scenario ... --script ...`.
  **Omit `--play`**: the script starts playback at a defined device time. The generator prints
  the default simulator path; use `sim/build328/leafsim` for the v3.2.8 build.
- Run `python sim/tests/imu_filter_comparison.py` on the host; it invokes the built v3.2.8
  simulator through Docker and saves its fixtures/output/summary under `sim/build/`.
- Compile/run `sim/tests/gravity_projection.cpp` with a C++17 compiler and `-Isrc/vario`.
- Build the default simulator and run `sim/scripts/first-boot.txt` with
  `sim/recordings/thermal-climb.json` and `--play` for the legacy startup regression.

## Startup investigation (2026-10-21)

The next active task is to distinguish **first available sensor data**, **trustworthy gravity
direction**, **qualified climb output**, and **leaving the splash screen**. These are different
milestones. No shorter warm-up or flight-validity claim follows from the code audit alone.

### Current boot/wake sequence, as implemented

| Stage | Current behavior | Reason / tuning status |
| --- | --- | --- |
| Cold driver initialization | Install I2C callbacks, wait 10 ms, call `wake()` | Initial boot allowance, following ST's example; not a multi-second stabilization wait |
| Every driver wake/recovery | Invalidate pending motion and filter; check WHO_AM_I; software POR | Starts a known configuration; POR helper itself waits 30 ms |
| Sensor configuration | BDU, +/-4 g, +/-2000 dps, accel/gyro/SFLP at 60 Hz | Required to produce the selected measurements |
| Bias/orientation initialization | Load commissioned gyro bias from NVS (otherwise zero); call ST's `lsm6dsv_sflp_game_gbias_set()` | The helper resets SFLP and seeds its initialization with a current accelerometer reading; it does not average stationary readings at ordinary boot |
| FIFO enable and acquisition | Batch accel + gravity; stream FIFO; poll at approximately 20 Hz | Raw records may arrive before Leaf publishes anything; first arrival is not yet measured |
| Leaf warm-up hold | Drain/pair records but publish none until 3000 ms after FIFO configuration | Conservative Leaf policy, not an identified ST requirement for a 3-second delay |
| Barometer startup (in parallel) | Discard 20 pressure readings; the next reading establishes altitude | Approximately 1 second at 20 Hz; separate from SFLP settling |
| Climb-filter qualification | Process 20 fresh pressure/motion updates after publication starts | Approximately 1 additional second at 20 Hz; sample count, not measured convergence |
| Splash completion | Barometer + IMU progress complete AND six splash renders | Display normally updates at 2 Hz; roughly 3-second presentation floor, overlapping the acquisition waits, plus frame scheduling |

Nominal IMU qualification is therefore **configuration time + about 4 seconds**, assuming
fresh pressure and regular scheduling. The approximately 1-second barometer wait and 3-second
splash minimum mostly overlap that interval; they must not be added again as sequential waits.
Actual user-visible boot additionally includes other peripheral initialization and any disclaimer.
Wake from charging currently follows the same IMU POR/configure/seed/hold path as ordinary boot,
apart from the cold `init()` 10-ms delay. ESP32 idle between normal polls does not restart SFLP.

`IMU::processGravity()` directly satisfies the old `STARTUP_NON_VERT_SAMPLES` counter. The
old orientation/gravity-acceptance gate and its 8-second escape path do not delay normal SFLP
startup. Only the 20 filter updates remain. The progress bar counts 20 discarded barometer
samples plus 20 filter samples; it does not measure the fixed 3-second hold or gravity accuracy.
The driver's `Ready` status means the hold expired, which can precede valid climb by a second.

Code references: `src/vario/hardware/lsm6dsv.cpp`,
`src/libraries/LSM6DSV/src/lsm6dsv_reg.c`, `src/vario/instruments/imu.cpp`,
`src/vario/instruments/baro.cpp`, `src/vario/taskman.cpp`,
`src/vario/ui/display/display.cpp` and `display_fields.cpp`.

### What the new IMU changes

ST documents separate physical sensor turn-on/filter settling and embedded fusion initialization.
The datasheet lists typical gyroscope turn-on at 30 ms; that is **not a guarantee of 30-ms
gravity convergence**. Its reference fusion example uses a 10-ms boot allowance and then polls
fusion output, without Leaf's fixed 3-second publication hold.

AN5922 section 6.5.1 describes seeding saved gyro bias when stillness cannot be guaranteed.
The procedure resets SFLP and supplies current acceleration for initialization. Leaf invokes
this path even for a missing calibration (with zero bias), so it need not deliberately ramp
from a fixed assumed level orientation. Verify the resulting orientation/transient on hardware.
An airborne accelerometer reading includes maneuver acceleration; this initialization does not
prove a correct vertical during a sustained turn. Gravity magnitude near 1 g and a completed
sample counter also do not prove correct direction or converged acceleration bias.

Sources: [LSM6DSV datasheet](https://www.st.com/resource/en/datasheet/lsm6dsv.pdf),
[AN5922 section 6.5.1](https://www.st.com/resource/en/application_note/DM00945663-.pdf), and
[ST's fusion example](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lsm6dsv_STdC/examples/lsm6dsv_sensor_fusion.c).

### First probe and subsequent tuning

The v3.2.8 dev environment enables `LEAF_IMU_STARTUP_TRACE`; release leaves it disabled.
It records configuration/reset/sensor-enable/bias/FIFO milestones, first acceleration/gravity
records, first complete/published pair, first valid climb, and splash completion. While a USB
capture host is attached it records the newest complete pair at the host poll rate,
including the otherwise discarded first 3 seconds, plus projected acceleration, pressure/time,
barometer readiness, publication/filter validity, startup sample count, climb, bias and FIFO level.
The trace observes early pairs without publishing them or changing the existing holds.
Times are host observations, not reconstructed acquisition timestamps. Measure trace overhead
and confirm any tuned result with tracing disabled; simulator injection bypasses the sensor
and cannot qualify SFLP's physical startup behavior.

Capture using the PlatformIO Python:

```powershell
& 'C:\Users\oxoth\.platformio\penv\Scripts\python.exe' 'tools and analysis/utils/imu_test/capture_startup.py' --seconds 60
```

The helper auto-detects one Espressif port, sets RTS inactive/DTR active before opening,
preserves the raw log, and extracts CSV samples + JSON milestones under `.pio/imu-startup/`.
Keep the connection open across wake trials; attach before pressing Leaf's power button.
Offline extraction: `capture_startup.py --analyze <capture.log>`. Do not erase NVS to test
ordinary startup. Missing-calibration trials require a separate deliberately controlled step.

Run repeated flat, inverted, upright and side starts/wakes with the same saved calibration,
then gentle continuous rotation and controlled lift/lower. Record first data, gravity-direction
transient, vertical residual, first qualified climb and first run screen separately. For a
stationary trial only, compare gravity direction with normalized acceleration and a settled
reference; agreement during motion cannot establish true gravity. Add missing-bias and sustained
turn trials before accepting a shorter moving-start policy.

Tune one gate at a time: first test shorter publication holds while retaining the 20-update
qualification, then evaluate the filter qualification separately. A shorter hold could let the
filter learn while the present code discards measurements, but feeding unsettled fusion can
also contaminate its bias state. Keep unqualified output unavailable. Use covariance/innovation
and recorded startup errors to assess a better readiness rule; don't require near-zero
acceleration or fixed orientation, which would reject genuine airborne starts.

The six-frame splash floor can be shortened independently once the user-visible startup policy
is chosen. An earlier run screen with explicit unavailable climb is also possible. A small
separate power optimization is to avoid initializing active fusion on USB charging-only boot:
`Power::initPeripherals()` currently configures the IMU and immediately sleeps it. Preserve
known chip power-down and recovery behavior when changing that sequence. Avoid adding a second
retained-fusion wake mode until its power/accuracy benefit is measured.

### First measured wakes with startup tracing

On 2026-10-21 the same MAC `7c:4f:ad:3b:ac:b0` was identified and the probe dev application
was written/verified in both OTA slots, preserving the bootloader and NVS calibration. Its
SHA-256 is `03fa4b252f50ddeb13c552ac46ae112a39823d62710206b02a2a678d719c5a4e`.
This replaces the previously installed diagnostic application; the previous checkpoint's
saved artifacts are retained. The probe binary/ELF are also saved locally under
`.pio/imu-validation/startup-probe/`. Startup holds and validity behavior are unchanged.

The user performed two charging-to-operating wakes, flat screen-up and upright, with the
same saved gyro bias and one serial connection. Raw capture: `.pio/imu-startup/flat-wake.log`;
CSV + JSON extraction use that same basename, and analysis is in `wake-statistics.json`.
There are 200 early sample rows per wake. These are **warm wakes on one board**, not cold
boots, missing-calibration tests, or moving-start acceptance.

| Observed milestone, relative to `configure_begin` | Flat | Upright |
| --- | ---: | ---: |
| Software POR complete | 31 ms | 31 ms |
| Saved gyro bias seeded | 59 ms | 59 ms |
| FIFO configured | 61 ms | 61 ms |
| First complete pair observed | 83 ms | 99 ms |
| Barometer first observed ready in pair trace | 627 ms | 648 ms |
| First published pair | 3081 ms | 3100 ms |
| First qualified climb | 4030 ms | 4050 ms |
| Splash completion | 4461 ms | 4231 ms |

The initial gravity vectors were approximately `[0.00275, -0.00104, 0.99942] g` flat and
`[0.02708, 0.99942, 0.00805] g` upright: the upright wake did not begin with flat gravity.
Compared with each trial's own mean direction over 7–10 seconds, first-pair direction differed
by 0.77 and 1.44 degrees respectively. It stayed within 0.2 degrees of that late direction
from approximately 1.28 and 2.35 seconds; within 0.1 degrees from 1.58 and 3.45 seconds.
These are observed direction changes, **not absolute attitude error or accepted readiness
thresholds**. Small physical movement, especially while holding upright, can contribute.

Projected vertical residual remained orientation-dependent: mean over 1–3 seconds was
approximately -0.00927 g flat and -0.000045 g upright. During qualified samples in the
4–6-second window, mean climb was -0.0801 / -0.0582 m/s and maximum absolute sampled climb
was 0.233 / 0.259 m/s (flat / upright). Over 7–10 seconds, mean climb was -0.0264 / +0.00073
m/s. These short-window statistics are not a repeat of the settled 60-second acceptance test.
Pressure noise/handling and bias convergence were not independently separated. In particular,
completion of 20 filter updates did not establish that startup transients had ended.

The measurements support testing a shorter publication hold, but do not establish that
immediate data should be trusted as climb output. First compare roughly 1- and 2-second
holds against the 3-second baseline using captured/replayed data and more physical starts;
retain the filter qualification initially and explicitly measure early climb/bias error.
Then decide whether to qualify by a better readiness rule, allow internal filter settling
before exposing climb, and/or shorten the splash minimum. Keep moving/turn starts in the
acceptance set; don't tune a rule solely to resting data.

Verification for the probe: repository formatter, v3.2.8 dev/release builds, v3.2.8 simulator
build and both existing biased/unbiased moving-start + fault/recovery replays passed.
Capture extraction was exercised with multiple wakes, interleaved diagnostics and partial
records, then with the real capture. Binary inspection confirmed early sample tracing is
present in dev and absent from release. The release image was built, not flashed. Physical
trace-overhead comparison, cold boot, inverted starts, moving starts and any shortened-hold
experiments remain open. The prior v3.2.6 regression result is unchanged; it was not rerun for
this development-only instrumentation pass.

## Acceleration dropout investigation (2026-10-21)

The user reported successful startup during rotation, followed by climb becoming `--` during
rapid raises/lowers. The first reproduction capture is
`.pio/imu-startup/acceleration-dropout-01.log`. It recorded **`Accelerometer clipped
(I2C errors=0)`** after a period of valid motion. The user continued gentle movement for
approximately ten seconds and then rested the device; neither restored output. No automatic
reconfiguration was recorded after this captured fault.

The existing +/-4 g driver treated a single saturated raw axis as a fatal hardware fault:
invalidate motion/filter, power down SFLP/gyro/accel, then retry configuration and repeat the
3-second hold and 20-update qualification. This explains multi-second interruption after
brief saturation. Ordinary acceleration does not have a stillness gate on the new path.
Code inspection also found that the three recovery attempts were a lifetime budget between
explicit wakes; successful recovered operation never replenished it. Repeated faults could
therefore leave the IMU disabled indefinitely. Prior attempts were not logged in this capture,
so exhaustion is a code-supported explanation for non-recovery, not a directly observed count.

The correction under test:

- Operate at +/-8 g, verify the full-scale register, use the matching FIFO conversion, and
  update projection bounds consistently. Keep commissioning stimulation/calibration at its
  existing +/-4 g setting; normal configuration restoration selects +/-8 g afterward.
- Reject an entire clipped FIFO time slot without calling the hardware fault/reset path.
  Preserve pairing in either tag order and continue draining to a usable pair. Count and
  rate-limit reports of clipping, including raw axis values.
- Complete clipped slots prove acquisition is alive, so sustained saturation does not itself
  restart the chip. Only fresh unsaturated data is published: the existing 250-ms freshness
  limit still makes climb unavailable if no usable acceleration arrives. No invented zero
  acceleration, stale-valid output or untested barometer-only fallback is introduced.
- Replenish the recovery budget after at least ten seconds since configuration with qualified
  output. Persistent consecutive bring-up/recovery failures remain bounded.
- Log every climb-validity transition in the dev build, with motion/pressure freshness,
  filter initialization/sample progress, reset count and clipped-sample count. Include time
  and retry count in fault reports and counters in periodic diagnostics.

ST documents +/-8 g as a supported accelerometer range; AN5922's gyro-bias initialization
procedure accounts for the current full scale. This does not establish dynamic accuracy or
noise acceptance at the new setting. Recheck stationary noise/bias, startup and moving data.
Sources: [LSM6DSV product/datasheet](https://www.st.com/en/mems-and-sensors/lsm6dsv.html) and
[AN5922 section 6.5.1](https://www.st.com/resource/en/application_note/DM00945663-.pdf).

`sim/tests/imu_acceleration.py` generates physically consistent pressure/acceleration input
with repeated +6 g specific-force peaks and negative acceleration, asserting continuous
validity on every 50-ms step during and after motion. This exercises the real instrument/filter
path, not the stubbed hardware FIFO. Projection checks cover high signed acceleration inside
range, rejection at full scale, freefall and invalid gravity. Physical saturation rejection,
power/fault recovery, and range/noise results must be recorded separately after testing.

### Saturation fix: first physical repeat

The saturation-handling dev image (SHA-256
`34ac489c1bd5b46ca3ddf343a49d40192d738504be6e4fef5a9ee26d1a1207ca`) was built and
verified in both app slots. The user confirmed values stayed available throughout repeated
raises/lowers and rotations. Capture `.pio/imu-startup/acceleration-fixed-01.log` recorded
**100 clipped raw samples**, zero hardware resets, zero FIFO mismatches and zero validity
losses after initial qualification (`t=56043 ms` through the last periodic report at
`t=255090 ms`). Thus the brief-saturation handling itself was exercised, not only the wider
range. Dev/release builds, projection tests, high-acceleration continuity replay and the
existing biased fault/recovery replay passed. The standalone hardware recovery-budget
replenishment path still needs deliberate fault-injection testing.

This repeat exposed a separate bias-learning defect. Sampled bias changed from approximately
-0.1 m/s² to as much as **-2.4708 m/s²** during motion, with false resting climb and slow
recovery afterward. The user independently observed this and requested excluding dynamic
samples from bias learning. Merely preserving availability does not qualify climb accuracy.
The archived motion-fix binary is in `.pio/imu-validation/motion-fix/`; its ELF was not retained
before the following build cleaned that environment's intermediate output.

### Bias-learning gate and shared legacy thresholds

Initial consideration of a 0.9–1.1 g window is superseded by the request to reuse the existing
ICM learning bounds. Code/history review established:

| Gate / limit | Existing ICM value |
| --- | --- |
| Raw total-acceleration learning window | 0.935–1.065 g (`abs(total - 1) <= 0.065`) |
| Vertical-residual learning window | +/-0.10 g |
| Learned scalar-gravity clamp | 0.94–1.06 g |
| Gravity plausibility check | 0.9–1.1 g |
| Commissioning acceleration check | Strictly between 0.8 and 1.2 g |

An older magnitude gate was +/-0.12 g (0.88–1.12 g), before `bd2fcee9` on June 23.
The manufacturing-offset change in `96df01d4` on July 15 widened the scalar clamp from +/-2%
to +/-6% and widened the learning gate from +/-5% to +/-6.5%, leaving margin for the clamp's
limits. The old gravity-performance backlog describes the pre-fix measurements, not the
current settings.

The new SFLP bias-learning gate now references the **same two tolerance constants** as the
existing ICM gravity learner. It pauses bias updates outside the magnitude/vertical windows,
on any discarded saturation reported since the previous update, and for two seconds after
the last disturbance. It does not pause motion/pressure processing, reset readiness, require
zero climb, discard genuine acceleration from prediction or change the old ICM learner.
Only the bias component of the Kalman observation gain is suppressed, with that same gain
used in the Joseph covariance update. Bias process noise remains unchanged for this first test.

Motion messages/logs/replay carry an optional `clippedSinceLastUpdate` bit after the existing
SFLP reset field. The accompanying acceleration is usable; the bit records missing saturated
slots that must not train a long-term correction. Old recordings omit it and remain valid.
The dev serial trace now captures ongoing paired motion/pressure/filter samples while a USB
capture host is attached, with clipping and learning-allowed fields, to characterize post-motion
settling; it is no longer limited to the first ten seconds. Release omits this trace.

Tests include the real Kalman implementation with bias held exactly constant during maneuver
and pressure disturbances while prediction still responds to acceleration; replay covers
under-observed maneuver impulses, pressure disturbance, clipping metadata on otherwise calm
data, two-second holdoff, and 1.064/1.066 g samples on either side of the shared upper gate.
Do not infer flight readiness or an ICM bias-filter migration from these changes.

### Shared bias gate: first physical repeat

The dev application with the shared gates was flashed and verified in both slots on the same
board, preserving NVS and the bootloader. SHA-256:
`d45ead12476d698697159844255c965a69cddfe442aa421673ed62e5c3915b1b`.
This is the current installed image, superseding the ungated saturation-fix image above.
Matching binary and ELF are retained in `.pio/imu-validation/bias-gate/`.

Capture `.pio/imu-startup/bias-gated-motion-01.log` includes ongoing paired samples with
learning/clipping flags; its CSV/JSON extraction and `bias-gated-statistics.json` hold the
measurements. The user kept the screen upward, established a resting baseline, performed
rapid raises/lowers, and rested the device again. The user confirmed availability throughout
and prompt recovery. In the analyzed initial 51.6-second segment:

- Detected dynamic samples ran from approximately 15.98 to 31.18 seconds after configuration.
- Peak observed total acceleration was **6.746 g**, with no clipping flag in that trial and
  no invalid samples after startup. This complements the preceding 100-clipped-sample
  continuity trial; the new gate's physical clipping-metadata path still needs a repeat
  that actually clips at +/-8 g.
- Serialized bias stayed exactly **-0.09819 m/s²** throughout the paused-learning interval.
- Mean climb in the first 0–2 seconds after the last detected dynamic sample was -0.136 m/s,
  with RMS 0.208 m/s. A short velocity transient remains; this is not instantaneous recovery.
- At 2–5 seconds after that sample: **mean -0.00624 m/s, RMS 0.03274 m/s**.
- At 5–10 seconds: mean +0.01017 m/s, RMS 0.03455 m/s.
- At 10–20 seconds: mean +0.00626 m/s, RMS 0.02567 m/s.
- The completed capture contains 4208 paired rows and no invalid samples after startup.
  Its final settled 60-second window (150.48–210.43 seconds after configuration) contains
  1200 samples: **mean climb +0.00161 m/s, RMS 0.03544 m/s**, mean bias -0.10003 m/s²,
  with learning allowed throughout. This meets the resting mean target for this board/face.

The maneuver/pressure-disturbance replay, upper/lower gate boundaries, independent vertical
gate, clipping metadata/holdoff, direct Kalman frozen-bias test, high-acceleration continuity,
existing biased recovery replay and legacy/new filter comparison all passed. The existing
comparison still preserves low sustained acceleration and moving startup. Formatter,
`leaf_3_2_8_dev`, `leaf_3_2_8_release` and `leaf_3_2_6_release` builds passed. Firmware builds
were run sequentially; only the v3.2.8 dev image was flashed. The serial capture is closed.

To reproduce the added checks, generate recordings with `python sim/tests/imu_acceleration.py`
and `python sim/tests/imu_bias_maneuver.py`, then use each printed v3.2.8 simulator command
(without `--play`). The direct filter test compiles/runs in the documented Docker environment:
`g++ -std=c++17 -Isrc/vario -Isim/hal/include sim/tests/kalman_bias.cpp src/vario/math/kalman.cpp -o sim/build328/kalman_bias && ./sim/build328/kalman_bias`.
Keep the existing recovery replays, projection test and `imu_filter_comparison.py` alongside
these checks.

This is a successful first fixed-orientation bench test, not a six-face, temperature,
coordinated-turn, flight or manufacturing qualification. Next compare rotation plus
translation, orientation-dependent bias adaptation, actual saturation with the learning gate,
and noise/latency on several boards. Keep the old ICM estimator until its separate migration
is requested and validated with real ICM data.

## Complete test commissioning, 2026-10-21

The local factory interface completed commissioning for MAC `7C:4F:AD:3B:AC:B0` using
release firmware `0.2.4-6ffd+h3.2.8`. The selected release application's SHA-256 before the
subsequent notes-only change was
`6c9256272c26cdb05ff6d311f09ee9ae0081b4ac3f37aa580b4a2f12371b64b7`.
The server reported `status=success`, every automatic/interactive test passed, and Leaf
acknowledged successful commissioning. SD formatting/restart, detailed-results retrieval,
temporary database persistence, and clearing the device's result file all completed. The
database was in memory for this test; no production database was fetched or changed. This
exercises the persistence code but does not establish durable database retention.

IMU results:

- Accelerometer electrical responses, X/Y/Z: 265.984, 264.301, 176.095 mg; all passed.
- Gyro electrical responses, X/Y/Z: 399.686, 415.562, 411.250 dps; all passed.
- Stationarity passed; saved gyro bias: -0.4214, +0.2310, -0.2163 dps. Operational
  configuration restored with zero I2C errors.
- Final IMU check: acceleration 0.98849 g, gravity 0.999471 g, projected residual
  -0.0109814 g, and climb +0.00404984 m/s. This is a single reading, not a resting-window
  accuracy/noise qualification.
- Interactive vario: 1.25 m altitude change, maximum climb +1.88 m/s, minimum -1.21 m/s.
- POWER passed with USB present, charging true, 88% battery and 4071 mV. The earlier
  charging-false failure did not recur in this run; its cause remains open.

The IMU notes have been shortened for subsequent runs: one three-axis electrical-response
line per sensor, one gyro-bias/stationarity/save-result line, and the final IMU result. Normal
output is four lines rather than ten. Numeric responses remain in X/Y/Z order, with units;
faults retain extra electrical/configuration/I2C diagnostics. Test thresholds, sample counts,
calibration behavior and normal motion processing are unchanged. Existing completed-session
notes remain the original firmware output.

Formatter, `leaf_3_2_8_release` build and diff checks passed for the notes change. No Leaf USB
port was present at verification (only the unrelated COM4 device), so the shorter-note firmware
has not been flashed or physically rerun. The commissioned release image and its ELF are
archived locally under `.pio/imu-validation/commissioning-pass/`; the new release binary/ELF
are under `.pio/imu-validation/commissioning-notes/`. The existing local database's SHA-256
was unchanged after the test session.

Next checks, in order:

1. Reboot and sleep/wake without erasing NVS; verify the commissioned gyro bias is restored
   and moving startup still works. Check retention across an application update as well.
2. Record resting windows on all six faces, including transitions between faces. Measure
   mean/RMS climb, residual acceleration and bias settling; use the existing +/-0.05 m/s
   60-second mean-climb target as engineering acceptance. Do not require six-face production
   calibration until those measurements justify it.
3. Combine rotation and translation, including starts while moving and actual acceleration
   clipping with the bias gate enabled. Verify output continuity and prompt recovery.
4. Exercise movement during commissioning, cancellation/retry, missing/corrupt calibration,
   and communication failures. Verify safe configuration restoration and successful recalibration.
5. Extend to more boards, temperature, power/current and representative flight recordings;
   turn the measured residual/noise limits into production self-test criteria.

## Calibration-retention follow-up, 2026-10-21

The commissioned board was updated with the diagnostic application only, writing both OTA
slots and preserving the bootloader/NVS. Image SHA-256:
`0e57e9bf5a5c206831da12d4529b9b4e997291cc75ac26945ff506cbdc11e9ec`.
This image also includes the shorter self-test notes. Both application-slot hashes verified.
The initial release USB port COM44 changed to the ROM/native interface COM42 on the first
upload attempt; the retry succeeded. Recording uses the same non-resetting DTR/RTS settings
as earlier probes, with automatic reconnection across physical power loss.

The first wake after the application update reported `saved_bias_seeded` at 59 ms and valid
climb at 4031 ms after configuration began. The user then turned Leaf off, unplugged USB for
five seconds, reconnected it into charging mode, and turned it on again. Device uptime reset;
the trace reported `saved_bias_seeded` during cold initialization (device time 819 ms) and
again at the following wake (59 ms after configuration). Climb became valid at 4030 ms, and
the user confirmed normal output. No commissioning/self-test or NVS erase was repeated.
This verifies valid saved gyro calibration was reused across an application update, cold
restart and charging-to-run wake on this board; the startup diagnostic does not print the
three saved numeric bias values, so it is not a byte-for-byte calibration comparison.

The completed local recording is
`.pio/imu-startup/calibration-retention-20261021-111426.log`; matching diagnostic binary/ELF
are under `.pio/imu-validation/calibration-retention/`. The recorder is stopped and the serial
port released. That diagnostic image was used for these measurements; the later release
installation is recorded below. Six-face resting measurements follow.

### Six-face resting measurements

The screen-up position after the cold restart was held still. In configuration session 3,
the 60-second window at 20-80 seconds after configuration contained 1200 samples: mean climb
-0.007762 m/s, RMS 0.036993 m/s, zero invalid/clipped-flag samples, with bias learning allowed
throughout. Mean acceleration was 0.989422 g; projected residual was -0.0100572 g and mean
Kalman bias -0.098495 m/s². Mean gravity vector was approximately (-0.01012, -0.00024, +0.99942) g.
This face passes the +/-0.05 m/s mean-climb engineering target. Summaries are stored locally
beside the capture as `calibration-retention-20261021-111426.windows.json`.

Screen-down then produced an orientation-dependent transient that the user reported took
about 30 seconds to settle. The last transition into a persistent negative-Z orientation was
at session-3 elapsed time 325078 ms. Relative to that transition:

| Seconds afterward | Mean climb, m/s | RMS climb, m/s | Bias at window end, m/s² |
| --- | --- | --- | --- |
| 0-5 | +0.17060 | 0.18540 | -0.06126 |
| 5-10 | +0.07912 | 0.08627 | -0.01054 |
| 10-20 | +0.04389 | 0.06161 | +0.03711 |
| 20-30 | +0.02588 | 0.05379 | +0.06498 |
| 30-40 | +0.01353 | 0.03581 | +0.07331 |
| 40-60 | +0.00266 | 0.03227 | +0.08061 |

The 60-second screen-down resting window at 20-80 seconds after that transition contained
1200 valid samples: mean climb +0.006755 m/s, RMS 0.038524 m/s, no invalid/clipped-flag samples,
and learning allowed throughout. Mean acceleration was 1.006297 g, projected residual
+0.0082456 g and mean bias +0.072406 m/s². Mean gravity was approximately
(+0.07310, +0.01230, -0.99529) g, so the supported device was slightly tilted.

Both measured faces pass the steady mean-climb target, but the orientation-change delay is
an open behavior problem, not a pass hidden by settling time. The opposite residual signs
suggest an accelerometer offset component. Complete the remaining faces and quantify offset
versus scale/attitude error before deciding whether to add commissioning accelerometer
calibration or change bias dynamics. Avoid solving this solely by faster unrestricted bias
learning, which could again absorb actual motion.

Upright (+Y gravity) also passed. Its final persistent orientation began at 489628 ms;
the 20-80-second window contained 1200 valid samples, mean climb +0.000187 m/s and RMS
0.037727 m/s, with no clipped flags and learning allowed throughout. Mean total acceleration
was 0.999975 g, projected residual +0.0003645 g and mean Kalman bias +0.002866 m/s². This
orientation's raw residual was substantially smaller than either Z-facing position.

Upside-down (-Y gravity) passed as well. Its final transition was at 610128 ms; the
20-80-second window contained 1200 valid samples, mean climb -0.003508 m/s, RMS 0.032322 m/s,
no clipped flags, and learning allowed throughout. Mean total acceleration was 0.994098 g,
projected residual -0.0038075 g and mean bias -0.036692 m/s².

Both X-facing positions then passed. Left edge is +X gravity (last transition 729628 ms);
right edge is -X gravity (858878 ms). Their windows likewise cover 20-80 seconds after the
last persistent orientation transition. All six windows contained 1200 samples each, with
zero invalid/clipped-flag samples and bias learning allowed throughout:

| Position | Mean climb, m/s | RMS climb, m/s | Mean total acceleration, g | Mean projected residual, g | Mean learned bias, m/s² |
| --- | --- | --- | --- | --- | --- |
| Screen-up (+Z) | -0.007762 | 0.036993 | 0.989422 | -0.0100572 | -0.098495 |
| Screen-down (-Z) | +0.006755 | 0.038524 | 1.006297 | +0.0082456 | +0.072406 |
| Upright (+Y) | +0.000187 | 0.037727 | 0.999975 | +0.0003645 | +0.002866 |
| Upside-down (-Y) | -0.003508 | 0.032322 | 0.994098 | -0.0038075 | -0.036692 |
| Left edge (+X) | +0.004841 | 0.037057 | 0.997773 | -0.0010585 | -0.012812 |
| Right edge (-X) | +0.005419 | 0.035607 | 0.999713 | +0.0006531 | +0.005238 |

The six means pass the +/-0.05 m/s target, with 7200 valid samples in the measured windows.
This does not close orientation-change settling: the screen-up/down flip still produced a
noticeable false-climb transient. The right-edge window had more small support/attitude
variation (Y/Z raw standard deviations 0.00371/0.00536 g versus roughly 0.0003-0.0019 g
per axis on the other faces); all remained within the learning gates.

An exploratory sphere fit to the six raw acceleration means estimates body-axis offset
(-0.001015, +0.003222, -0.008493) g and common radius 0.997902 g, with 0.000566 g radial RMS
residual. A separate four-parameter fit of projected residual against gravity direction gives
0.000492 g RMS residual across those same six means. These fits support investigating a
fixed body-axis accelerometer calibration to reduce the scalar Kalman bias's orientation
changes; they are training-data fits, not independent calibration validation or proof that
the entire error is accelerometer offset. SFLP gravity magnitude, scale, attitude, temperature
and support variation also need consideration. Nothing has been applied or saved to Leaf.
The exploratory fit is in the local `.windows.fit.json` beside the capture. Validate a candidate
correction on separate intermediate-angle/rotation data before adding production calibration.

### Combined raise/lower/rotation and recovery

The user raised/lowered Leaf while rolling and tilting it, then rested it screen-up. Detected
disturbances span session-3 elapsed time 1000078-1035328 ms (35.25 seconds). All 706 motion
samples stayed valid; peak total acceleration was 7.061681 g. Learning was paused throughout
all 706 samples and the serialized bias remained exactly -0.00072 m/s². No clipping flags were
recorded, so this does not validate actual saturation at the new operating range. The user
confirmed values stayed available.

Recovery after the last detected disturbance:

| Seconds afterward | Mean climb, m/s | RMS climb, m/s |
| --- | --- | --- |
| 0-2 (learning holdoff) | -0.203846 | 0.245784 |
| 2-5 | -0.050161 | 0.063012 |
| 5-10 | -0.038407 | 0.051474 |
| 10-20 | -0.017714 | 0.037179 |
| 20-30 | -0.006055 | 0.025268 |
| 30-45 | -0.001469 | 0.037865 |
| 45-60 | -0.000182 | 0.037294 |
| 60-80 | +0.002041 | 0.033879 |

The 20-80-second resting window contained 1200 valid samples, mean climb -0.000741 m/s,
RMS 0.034571 m/s and mean bias -0.101343 m/s². Returning from the X-facing orientation to
screen-up requires a different scalar correction; the motion gate prevented high-g samples
from changing the bias, but cannot itself remove that orientation dependence.

Across the whole final configuration, all 23606 samples after first-valid climb remained
valid. Final diagnostics showed 69848 matched pairs, zero mismatches, clips, FIFO resets or
retries. Detailed offline results are `.motion.json`, `.windows.json`, `.windows.fit.json`,
and the capture helper's `.csv`/`.json` beside the raw log. This completes the initial retention,
six-face and combined-motion walkthrough on this board; independent validation remains open.

### Decision: defer accelerometer commissioning calibration

On 2026-10-21 the user deferred the proposed accelerometer-calibration prototype to avoid
additional commissioning work. Expected use is a device mounted in a chosen position and
left there. Keep commissioning unchanged: electrical tests and saved gyro bias remain;
do not add a six-face operator procedure, saved accelerometer coefficients or automatic
accelerometer calibration. The six-face measurements above are engineering evidence only.

Retain the current gated runtime Kalman bias correction. The measured 20-30-second settling
after a large orientation change remains a documented limitation, not a requirement to add
factory calibration now. Revisit the proposal if representative mounted-use/flight testing
shows material false climb, or new board/temperature measurements justify the operator cost.
Fixed mounting does not eliminate pitch/bank changes relative to gravity, so sustained-turn
and normal flight validation remain necessary. The exploratory coefficients have not been
applied or saved to the device.

### Follow-up proposal: reuse the existing flat stationary window

The user subsequently asked whether the flat commissioning position could supply one saved
acceleration-bias measurement without extra operator work, since flat or 30-45-degree pitched
mounting is common. This is a narrower proposal than the deferred six-face calibration and
was initially a proposal; the later implementation below supersedes that status. It does not
require a new six-face production procedure.

A useful approach is to capture a stationary mean residual in the normal operating
configuration after electrical stimulation is disabled, save its orientation/validity metadata,
and evaluate using it as the initial Kalman bias estimate for similar mounting orientations.
Reuse the existing stillness/warm-up period; retain uncertainty and runtime learning. The
current electrical/gyro-calibration sampling uses +/-4 g, while operation uses +/-8 g, so do
not silently assume a residual from the stimulation-test configuration is the operating one.
One flat position cannot identify all three axis offsets and scale errors. A fixed scalar
subtraction at every orientation could worsen other mounts; intermediate-angle validation
is still needed. Such a seed may reduce initial false climb but does not by itself shorten
the fixed startup gates or resolve later large-orientation-change settling.

### Next work after this walkthrough

1. Validate representative fixed-mount use, moving startup, normal pitch/bank changes and
   sustained turns with the current estimator. Measure useful response and false climb;
   accelerometer-calibration prototyping is deferred by the decision above.
2. Validate on more boards and across temperature; establish production residual/noise/settling
   thresholds without adding accelerometer-calibration steps to commissioning.
3. Complete commissioning rejection/cancellation/corrupt-record checks, actual clipping with
   the learning gate enabled, communication/power-current tests, and representative flight data.
4. Re-run self-test with the shortened notes and validate the final normal release image on
   hardware. Current self-test pass evidence is from the earlier, more verbose release output.

## Release firmware installed, 2026-10-21

At the user's request, flashed the previously verified `leaf_3_2_8_release` application from
`.pio/imu-validation/commissioning-notes/firmware.bin` to both `0x10000` and `0x340000` on
MAC `7C:4F:AD:3B:AC:B0`. Both 2286384-byte app-slot writes passed hash verification. The
bootloader, partitions and NVS were not written. Image SHA-256:
`7ef3612add722a8217e33e7c8889324bc2dfc886530f6a1f4a7e43d254b2fc3e`.
Its matching ELF is archived beside the binary. The image includes the shorter IMU self-test
notes and omits the development startup trace.

Esptool hard-reset the board after upload; it re-enumerated from native COM42 as the release
USB composite device on COM44. Flash log: `.pio/imu-startup/release-closeout-flash.log`.
This confirms application installation and USB enumeration, not the remaining user-visible
release smoke checks: rest/moving startup, charging/off/on behavior and shortened-note self-test.

### Second complete test commissioning and GPS result interpretation

The next test commissioning also completed successfully on release firmware
`0.2.4-a5dd+h3.2.8`; every reported self-test passed. The shortened IMU output was verified
on hardware: two three-axis response lines, a stationary/saved gyro-bias line, and the final
IMU result. Saved gyro bias was (-0.4130, +0.2618, -0.2037) dps; final acceleration 0.989943 g,
gravity 0.999461 g, residual -0.00951897 g and climb +0.0280679 m/s. This closes the
shortened-note/self-test rerun item, without establishing the other release smoke checks.

GPS_FIX logged PASS after 77 seconds with `Fix_sats=8`; the later factory poll showed 10
satellites. The first commissioning had logged PASS with `Fix_sats=0`, while its later poll
showed six. Code inspection explains how this can occur:

- `SelfTest::testGPSfix()` passes when TinyGPSPlus's `sentencesWithFix()` counter increases
  after the test starts. That counter advances for checksum-valid RMC/GGA messages reporting
  a fix; passing does not depend on `fixInfo.numberOfSats`.
- The logged `Fix_sats` comes from `fixInfo.numberOfSats`, populated only when a GSV
  satellite-display message group completes. It counts accepted satellite entries in view,
  not satellites used in the positioning fix. It can lag the fix message or hold a zero count.
- The GGA satellites-used field is separately parsed as `gps.satellites`. The GSV list also
  filters satellite identifiers to 1-80, so it is not a complete count for all constellations.

The first zero is consistent with message timing or an incomplete/empty GSV snapshot; no
raw NMEA from that instant was retained to prove its exact cause. Treat this as a reporting
source/label mismatch, not evidence by itself that GPS had no fix. Recommended follow-up is
to capture a fresh, consistent GGA fix/satellites-used snapshot for the result, and label any
separate GSV count as satellites in view (unknown if not yet available). Do not substitute a
possibly stale GGA count when the pass was triggered by an earlier RMC message. No GPS code
or pass criteria have been changed in this investigation.

## Flat commissioning bias, GPS records and audio restoration, 2026-10-21

At the user's request, the existing flat commissioning step now captures a single acceleration
bias reference for v3.2.8. Pre-v3.2.8 ICM gravity estimation/calibration is unchanged; extending
the reference to ICM is deferred to a future estimator migration. The shared GPS/audio fixes
below apply to both hardware paths.

After electrical tests and gyro-bias capture, normal +/-8 g/SFLP operation is restored. During
the existing three-second warm-up, discard the first second then average 100 paired samples
at 60 Hz. The operator prompt remains flat/still; there is no new operator step or separate
boot-time stationary calibration. Capture requires near-flat +Z gravity (`gz > 0.95` after
normalization), mean acceleration between 0.9 and 1.1 g, per-axis acceleration/gravity-direction
variance below 0.0004, and projected-residual variance below 0.0004 g². Clipping, motion,
invalid data and the self-test watchdog prevent accepting a bad reference. The watchdog was
subsequently extended from 15 to 25 seconds for bounded stationary-window retries below.

The versioned `FlatAccelerationBias` record stores mean projected residual and normalized
reference gravity in NVS `imu328/flatBias`, independent of the unchanged `gyroBias` record.
Saving includes immediate readback/validation; failure fails the IMU test. A concise
`FlatBias_g`/`Saved` result adds one line to the shortened notes. Missing/corrupt references
are ignored and retain the prior zero-seed behavior. NVS erasure clears both records.

On ordinary initialization/recovery, the scalar reference is projected by the alignment of
the current and recorded gravity directions and used only within 60 degrees of the reference.
It seeds the Kalman acceleration-bias state; it is not continuously subtracted from raw
acceleration. Other orientations use the original zero seed. Initial uncertainty remains
0.25 m/s², and runtime learning/gating is unchanged, so a different mounting error can still
be learned. Existing startup gates and later-orientation settling are unchanged. This is
one-orientation initialization assistance, not a full body-axis accelerometer calibration.

GPS self-test now waits for a checksum-valid, newly received GGA observation after test start,
with nonzero fix quality and satellites used, no older than three seconds. Status and used
satellite count are copied from that same GGA sentence. `Fix_sats` therefore records satellites
used in the passing fix, independent of GSV display updates; RMC alone or stale/no-fix GGA
cannot pass this test. The existing timeout/cancellation behavior remains.

Vario audio stays muted through the GPS test, awaiting-confirmation and COMPLETE screens.
Actual removal of the final confirmation/results page restores the configured vario volume;
covering it with another modal does not. Configured Off stays Off, and quiet-mode/power mute
behavior is preserved. Starting another self-test also clears the prior test's temporary mute.

Verification so far: repository formatter/diff checks; real Kalman/reference tests covering
flat/45-degree/side/inverted/malformed records, retained learning and legacy behavior; and
simulator integration exercising stale GGA, fresh RMC, zero GSV, bad checksum, no-fix GGA,
GP/GN talkers, coherent satellites-used results, final-page cover/removal and Low/High/Off
volume restoration. All passed, as did `leaf_3_2_8_release` and `leaf_3_2_6_release` builds.
The build's generated version header was restored to the checked-in baseline after verification.
The candidate binary/ELF are archived under `.pio/imu-validation/commissioning-bias/`;
build/test logs are `.pio/commissioning-*`.

Installed the 2290832-byte release application into both OTA slots on the same board, with
both hashes verified and bootloader/NVS untouched. The USB app port COM44 transitioned to
COM42 on the first upload attempt; the single retry succeeded. Binary SHA-256:
`415e4d410fd3d96484a509f8c3565b34c8490644a29d5501f39cdbc98d429491`.
Flash logs are `.pio/imu-startup/commissioning-bias-flash{1,2}.log`. A fresh commissioning
run and physical COMPLETE-screen/audio-dismissal verification were requested after installation.

### Commissioning rejection during hardware verification

The first hardware commissioning with the new flat-reference code failed in the existing
gyro stationarity phase, before flat-reference acquisition. Electrical responses all passed:
accel (266.033, 264.276, 177.388) mg; gyro (399.896, 415.828, 411.208) dps. The candidate
gyro bias (-0.4109, +0.2723, -0.1610) dps was plausible, but `Stationary=0`, `Saved=0`;
configuration restored with zero I2C errors. All other tests passed, including GPS_FIX after
28 seconds with five satellites used, versus eight in the later GSV/display snapshot.
The user confirmed Leaf was flat and untouched throughout the prompt.

The original failure log did not include individual variances or mean acceleration, so its
precise failing limit is unknown. Do not assume operator movement or weaken the thresholds.
Added failure-only mean-acceleration and per-axis gyro/acceleration standard deviations, plus
100 raw stationary-window samples in the diagnostic build only, to distinguish noise,
post-stimulation settling and sampling issues.

Three diagnostic repeats passed without changing thresholds. The operator reported normal
typing on the same desk, which is an expected commissioning condition. Per-axis gyro
standard deviations (dps) were (0.12894, 0.15301, 0.04725),
(0.15507, 0.12265, 0.05187), and (0.13461, 0.12734, 0.05622), against the existing
0.2 dps limit. Acceleration standard deviations were at most 0.00644 g, against 0.02 g.
The first second of the first repeat was quieter than its remaining four seconds; this is
consistent with desk vibration, but does not identify the precise original failing limit.
The captured logs are `.pio/imu-startup/stationarity-01.log` and
`.pio/imu-startup/stationarity-repeats.log`.

Flat-reference capture and immediate NVS readback passed in all three repeats:
-0.009237, -0.009068 and -0.009312 g. Subsequent reboot traces confirmed the saved
reference was restored. This verifies physical capture/persistence, not temperature coverage
or general multi-orientation calibration. Physical COMPLETE-screen/audio restoration remains
pending completion of the interactive tests.

Added bounded automatic recovery for a disturbed gyro measurement: retain all original
mean/variance limits and require a fresh complete 100-sample window, with at most three
attempts. Rejected samples are discarded and never saved. Persistent disturbance still fails;
electrical, I2C, storage and flat-reference failures do not receive a blanket retry. Increase the
driver watchdog from 15 to 25 seconds to accommodate the extra windows while remaining
inside the existing 30-second outer IMU timeout. No extra operator step is introduced.
Both updated v3.2.8 diagnostic and release builds passed. The diagnostic image was installed
in both OTA slots, preserving NVS/bootloader. A deliberate repeated desk-tapping test then
exercised all three windows: gyro standard deviations were (0.54008, 0.74460, 0.10662),
(0.25584, 0.35727, 0.06455), then (0.02322, 0.02865, 0.03414) dps after tapping stopped.
The first two windows were rejected and discarded; the third passed and saved
(-0.4130, +0.2765, -0.1827) dps. Flat capture subsequently passed with -0.011142 g and
the final IMU test passed. This demonstrates recovery after stronger intermittent disturbance;
it does not establish that every desk/vibration spectrum will pass, or that the original
unlogged failure was definitely vibration. Logs and per-window statistics are in
`.pio/imu-startup/stationarity-retry-test.{log,stats.json}`.

The release binary/ELF are archived in `.pio/imu-validation/stationarity-retry-release/`;
application SHA-256 `3489c3f571e545aa4b90ee37aad0c11c2c1fbecc94685bf9e70c6662f077e8d6`.
The remaining interactive checks were completed and the device reported ALL PASS.
GPS_FIX recorded six satellites used after 96 seconds, while the later display/API
satellite count was eleven; the record now reflects the qualifying GGA observation.
The complete SD report is saved at `.pio/imu-startup/stationarity-retry-self-test.txt`.
The operator confirmed all tests passed and audio behaved as expected. The LCD showed
"awaiting commissioning confirmation", as expected for this direct diagnostic test outside
the factory-interface session; no production commissioning record was written.

Installed the 2291632-byte release application into both OTA slots, with hashes verified
and bootloader/calibration NVS preserved. The single detected Espressif port was COM42;
upload succeeded without a retry. After reboot the device endpoint reported release version
`0.2.4-a5dd+h3.2.8` (without `.dev`). Upload log:
`.pio/imu-startup/stationarity-retry-release-flash.log`. The test server's application source
continues to point at the updated local v3.2.8 release build. The production database remains
unchanged. Physical testing above used the matching diagnostic build; the release build
compiled and was installed/booted, but was not put through another complete commission.

## Optional older-hardware estimator migration

The user requested that the backlog retain the option to migrate **v3.2.7 and earlier
ICM-20948 hardware** to the v3.2.8 Kalman acceleration-bias approach, together with captured
flat-acceleration offsets during the existing commissioning step. This remains deferred and
is not implemented by this hardware-support PR.

Keep ICM quaternion orientation, replace its separate learned gravity magnitude with the
barometer-observed Kalman bias state, and seed that state from a validated flat commissioning
reference for similar mounts. Capture the residual before any legacy gravity correction so
the reference is not already corrected. Apply only one bias/gravity correction mechanism.
Reuse the current 0.935–1.065 g total-acceleration and +/-0.10 g projected-residual learning
windows, with clipping/holdoff protection and uninterrupted motion output.

No extra worker pose or six-face procedure is intended. Missing records on existing units
must allow normal moving startup and runtime learning without forced recommissioning.
Define hardware-specific storage/versioning and preserve legacy calibration/rollback behavior.
Validate across multiple ICM boards, manufacturing variation, temperature, orientations,
moving startup, turns and high acceleration before enabling the migration. The more detailed
scope and acceptance checks are in the
[gravity-performance backlog](vario-imu-gravity-performance.md#optional-icm-estimator-migration-deferred).

## Remaining assumptions and decisions

### Confirmed and no longer open

- Exact IMU, magnetometer, speaker, I2C addresses, top-side mounting, and +Y board direction.
- INT1 through IO expander P14; INT2 and magnetometer INT unused.
- Polling first, with future tap/double-tap support deferred.
- No initial magnetometer data/fusion or speaker firmware work.
- Startup while flying is mandatory; a 3-10 second warm-up is acceptable.

### Board and measurement checks

- Confirm the remaining v3.2.8 pins/peripherals and flash layout match the v3.2.7 baseline.
- Verify the actual sensor-vector signs and axis labeling; mounting information alone does not
  establish the driver's sign convention.
- Extend residual accelerometer/gravity measurements across temperature and orientation before
  finalizing the experimental Kalman bias correction or adding per-axis calibration.
- Finalize full scales, sample/filter rates, FIFO pairing, timestamp mapping, decimation, and
  startup/fault thresholds from measured latency, noise, and sustained-turn behavior.
- Validate saved gyro-bias reuse across temperature and resets, and define missing-calibration
  behavior that remains compatible with moving startup.
- Establish production commissioning thresholds. Additional accelerometer calibration and
  any six-position production procedure are deferred by the user's decision above.

### Product choice still open

For persistent IMU failure, the proposed initial behavior is a visible fault and muted vario
audio during recovery. Whether Leaf must continue with a tested **barometer-only climb mode**
has not been confirmed. Preserve this as an explicit decision rather than silently treating zero
acceleration or stale velocity as a fallback.
