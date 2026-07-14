---
title: Vario IMU Gravity Performance Backlog
description: Findings and future tuning notes for IMU gravity estimation and stationary climb-rate errors.
---

# Vario IMU Gravity Performance Backlog

## Context

Some test units report a persistent non-zero climb rate while sitting stationary. A diagnostic
`vario.csv` log was captured from one affected unit after adding IMU/baro/gravity/Kalman diagnostic
fields. The main test session included:

- Leaf flat on a table, reporting about `0.1` to `0.3 m/s` climb.
- Leaf pitched upright about 90 degrees, reporting reduced climb closer to `0.0` to `0.2 m/s`.
- Leaf returned flat on the table, with the higher stationary climb returning.

This suggests the accelerometer scale error is not equal across orthogonal axes.

## Current Findings

In the affected unit's flat orientation:

- Device/world vertical acceleration was around `1.048g` to `1.052g`.
- The gravity estimate was pinned at the current upper clamp of `1.020g`.
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

## Likely Cause

The current gravity estimate clamp is too narrow for at least some IMU modules. A stationary IMU
whose aligned accelerometer axis reads above `1.02g` cannot be learned correctly because the
estimate is clamped before it reaches the observed resting value. The Kalman filter then sees a
small constant upward acceleration even when Leaf is motionless.

There is also a related gate: `GRAVITY_UPDATE_ACCEL_TOLERANCE_G` is currently `0.05g`. If we expect
some stationary IMUs to read as much as 6% high or low, then stillness detection must accept at
least that same error range. Otherwise, the samples needed to learn the corrected gravity value can
be rejected before they update the estimate.

## Proposed Future Changes

- Expand the gravity estimate clamp from `[0.98g, 1.02g]` to `[0.94g, 1.06g]`.
- Increase `GRAVITY_UPDATE_ACCEL_TOLERANCE_G` from `0.05g` to at least `0.06g`.
- Consider using `0.065g` for the accel tolerance to avoid rejecting otherwise-stationary samples
  at the edge due to noise and quantization.
- Continue collecting `vario.csv` logs from additional units before implementing the change, so the
  final bounds cover observed IMU variation without making the estimator too willing to learn during
  real motion.
- After changing bounds, repeat stationary flat/upright tests and confirm:
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
