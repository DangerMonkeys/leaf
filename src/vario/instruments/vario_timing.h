#pragma once

#include <stdint.h>

// Output freshness is independent of whether the estimator must be reinitialized.
namespace vario_timing {
  constexpr uint32_t SENSOR_FRESH_MS = 500;
  constexpr double KALMAN_MAX_GAP_S = 1.5;
}  // namespace vario_timing
