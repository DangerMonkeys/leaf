#pragma once

#include <cmath>
#include <cstdint>

// A commissioning observation in one stationary orientation, not a three-axis calibration.
struct FlatAccelerationBias {
  static constexpr uint32_t VERSION = 0x3280001;
  uint32_t version = 0;
  float residualG = 0;
  float gx = 0, gy = 0, gz = 0;

  bool valid() const {
    const double norm2 = gx * gx + gy * gy + gz * gz;
    return version == VERSION && std::isfinite(residualG) && fabs(residualG) < .1 &&
           std::isfinite(norm2) && norm2 > .99 && norm2 < 1.01 && gz > .95;
  }

  double initialBias(double x, double y, double z) const {
    const double norm2 = x * x + y * y + z * z;
    if (!valid() || !std::isfinite(norm2) || norm2 < .81 || norm2 > 1.21) return 0;
    const double alignment = (gx * x + gy * y + gz * z) / sqrt(norm2);
    // Only seed similar mounts (within 60 degrees). Projection is a starting approximation;
    // the filter retains its original uncertainty and can learn a different correction.
    return alignment >= .5 ? residualG * 9.80665 * alignment : 0;
  }
};
