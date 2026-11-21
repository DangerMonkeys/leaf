---
title: Vario IMU Gravity Performance Backlog
description: Findings and future tuning notes for IMU gravity estimation and stationary climb-rate errors.
---

# Vario IMU Gravity Performance Backlog

**Historical findings; status clarified 2026-10-21.** The clamp/gate changes proposed below
were implemented in `96df01d4` on July 15. The current ICM learner accepts total acceleration
within **0.935–1.065 g** and vertical residual within **+/-0.10 g**, with learned gravity
clamped to **0.94–1.06 g**. References below to a +/-2% clamp and +/-5% sample gate describe
the original failing configuration. The v3.2.8 bias-learning gate now reuses the two current
sample tolerances; this does not replace the ICM's existing learner.

The v3.2.8 replacement plan is recorded in the
[ST IMU and Separate Magnetometer Migration Backlog](st-imu-magnetometer-migration.md).
The implemented v3.2.8 path uses a gated Kalman acceleration-bias state and a saved flat
commissioning reference; the ICM path for v3.2.7 and earlier is unchanged. The findings below
describe the original ICM issue.

The migration record also contains an experimental comparison against a Kalman acceleration-bias
state. If bench and flight validation support it, evaluate that shared correction for the ICM
while retaining its quaternion projection. Do not run both correction mechanisms together or
remove the ICM gravity filter based only on the new-board synthetic tests.

## Optional ICM estimator migration, deferred

Consider moving v3.2.7 and earlier ICM-20948 hardware to the v3.2.8 Kalman acceleration-bias
approach in a separate change. This is an option, not an approved replacement or part of the
initial v3.2.8 implementation. It could share the runtime bias estimator and flat commissioning
reference without adding an operator step or six-face calibration.

- Keep the ICM's quaternion orientation/projection. Feed world-vertical acceleration minus
  nominal 1 g into the bias-enabled Kalman filter, bypassing the separate learned gravity
  magnitude. Do not apply the old gravity correction and the new bias correction together.
- Reuse the current total-acceleration window **0.935–1.065 g**, projected residual window
  **+/-0.10 g**, and appropriate clipping/recovery holdoff to gate bias learning. Real motion
  must continue driving climb while bias learning is frozen.
- During the existing flat/still commissioning stage, average the uncorrected projected
  residual and reference gravity direction. Validate sample quality, save a versioned record,
  and verify readback. Avoid measuring the residual after the old gravity learner has already
  removed the offset. The current v3.2.8 implementation provides a starting design.
- Use the reference only as an initial Kalman bias for similar mounting orientations,
  including flat and 30–45-degree mounts; retain runtime learning and uncertainty. A single
  flat observation cannot establish all-axis offsets or scale errors.
- Define ICM-specific storage/versioning and update/rollback behavior. Existing commissioned
  units without a reference must keep working with a zero seed and runtime learning, without
  mandatory recommissioning. Preserve the existing ICM calibration records.
- Compare against the current ICM estimator on multiple units, including the original high/low
  tolerance outliers, across temperature, six resting orientations, moving startup, realistic
  climbs/turns and strong acceleration. Verify continuity, false climb, settling time, save/reboot
  retention and tolerance of ordinary desk vibration before selecting defaults.

Hardware evidence so far applies to one LSM6DSV board, not this proposed ICM path. See the
[migration follow-up](st-imu-magnetometer-migration.md#flat-commissioning-bias-gps-records-and-audio-restoration-2026-10-21)
and [commissioning backlog](factory-commissioning-followup.md) for completed tests and limits.

## Context

Some test units report a persistent non-zero climb rate while sitting stationary. A diagnostic
`vario.csv` log was captured from one affected unit after adding IMU/baro/gravity/Kalman diagnostic
fields. The main test session included:

- Leaf flat on a table, reporting about `0.1` to `0.3 m/s` climb.
- Leaf pitched upright about 90 degrees, reporting reduced climb closer to `0.0` to `0.2 m/s`.
- Leaf returned flat on the table, with the higher stationary climb returning.

This suggests the accelerometer scale error is not equal across orthogonal axes.

## Historical findings (before the July clamp/gate fix)

In the affected unit's flat orientation:

- Device/world vertical acceleration was around `1.048g` to `1.052g`.
- The gravity estimate was pinned at the then-current upper clamp of `1.020g`.
- The remaining vertical acceleration residual was about `+0.03g`.
- The Kalman/filter chain converted that residual into a persistent positive climb rate, commonly
  around `+0.18 m/s`.

In the upright orientation:

- The axis aligned with gravity was closer to nominal, around `1.00g` to `1.02g`.
- The gravity estimate was no longer stuck at the `1.020g` upper bound.
- The vertical acceleration residual was much closer to zero.
- The filtered climb rate dropped substantially.

The barometer did not appear to be the primary cause in this log. Baro altitude drift was much
smaller than the reported climb-rate bias, while the IMU vertical acceleration residual closely
matched the sign and persistence of the climb-rate error.

## Historical cause

The original gravity estimate clamp was too narrow for at least some IMU modules. A stationary IMU
whose aligned accelerometer axis reads above `1.02g` cannot be learned correctly because the
estimate is clamped before it reaches the observed resting value. The Kalman filter then sees a
small constant upward acceleration even when Leaf is motionless.

There was also a related gate: `GRAVITY_UPDATE_ACCEL_TOLERANCE_G` was `0.05g`. If we expect
some stationary IMUs to read as much as 6% high or low, then stillness detection must accept at
least that same error range. Otherwise, the samples needed to learn the corrected gravity value can
be rejected before they update the estimate.

## Historical changes (implemented in July 2026)

- Expand the gravity estimate clamp from `[0.98g, 1.02g]` to `[0.94g, 1.06g]`.
- Increase `GRAVITY_UPDATE_ACCEL_TOLERANCE_G` from `0.05g` to at least `0.06g`.
- Consider using `0.065g` for the accel tolerance to avoid rejecting otherwise-stationary samples
  at the edge due to noise and quantization.
- Remaining qualification: collect `vario.csv` logs from additional ICM units to assess the
  implemented bounds against observed variation and real motion.
- Repeat stationary flat/upright tests on those units and confirm:
  - `gravity_g` converges near the observed resting `awz_g`.
  - `vertical_accel_g` is near zero when stationary.
  - `climb_filtered_cms` settles near zero in multiple orientations.
  - Real movement still produces responsive climb/sink values.

## Open Questions

- How wide is the actual accelerometer scale-error distribution across a larger sample of Leaf
  units?
- Are high/low errors axis-specific enough that per-axis calibration should eventually replace a
  single gravity magnitude estimate?
- Should the diagnostic self-test record resting `accel_total_g`, axis values, and gravity estimate
  so manufacturing can identify outlier IMUs earlier?
