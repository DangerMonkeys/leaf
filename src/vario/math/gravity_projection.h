#pragma once

#include <cmath>

// Keep the LSM6DSV operating range, FIFO conversion and projection limit in agreement.
inline constexpr double SFLP_ACCEL_FULL_SCALE_G = 8.0;

// SFLP gravity and accelerometer specific force use the same sensor axes and g units.
// Positive result is upward acceleration. No yaw/heading or board mounting is required.
inline bool projectVerticalAcceleration(double ax, double ay, double az, double gx, double gy,
                                        double gz, double& vertical, double& gravity) {
  const double a2 = ax * ax + ay * ay + az * az;
  const double g2 = gx * gx + gy * gy + gz * gz;
  const double limit = SFLP_ACCEL_FULL_SCALE_G - .001;
  if (!std::isfinite(a2) || !std::isfinite(g2) || g2 < .81 || g2 > 1.21 || fabs(ax) >= limit ||
      fabs(ay) >= limit || fabs(az) >= limit)
    return false;
  gravity = sqrt(g2);
  vertical = ((ax - gx) * gx + (ay - gy) * gy + (az - gz) * gz) / gravity;
  return std::isfinite(vertical);
}
