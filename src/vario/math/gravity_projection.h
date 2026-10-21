#pragma once

#include <cmath>

// SFLP gravity and accelerometer specific force use the same sensor axes and g units.
// Positive result is upward acceleration. No yaw/heading or board mounting is required.
inline bool projectVerticalAcceleration(double ax, double ay, double az, double gx, double gy,
                                        double gz, double& vertical, double& gravity) {
  const double a2 = ax * ax + ay * ay + az * az;
  const double g2 = gx * gx + gy * gy + gz * gz;
  if (!std::isfinite(a2) || !std::isfinite(g2) || g2 < .81 || g2 > 1.21 || fabs(ax) >= 3.999 ||
      fabs(ay) >= 3.999 || fabs(az) >= 3.999)
    return false;
  gravity = sqrt(g2);
  vertical = ((ax - gx) * gx + (ay - gy) * gy + (az - gz) * gz) / gravity;
  return std::isfinite(vertical);
}
