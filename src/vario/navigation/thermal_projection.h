#pragma once

#include <math.h>
#include <stdint.h>

// Snapshot of local air motion, in 0.25 m/s steps. INT8_MIN denotes unavailable wind.
struct ThermalSampleWind {
  int8_t east = INT8_MIN;
  int8_t north = INT8_MIN;

  static ThermalSampleWind capture(bool valid, float speedMps, float directionRad) {
    if (!valid || !isfinite(speedMps) || !isfinite(directionRad) || speedMps < 0 ||
        speedMps > 20.0f) {
      return {};
    }
    // Direction is where the air is moving toward, in radians east of north.
    return {static_cast<int8_t>(roundf(sinf(directionRad) * speedMps * 4.0f)),
            static_cast<int8_t>(roundf(cosf(directionRad) * speedMps * 4.0f))};
  }

  void project(float sampleXM, float sampleYM, int16_t sampleAltM, int16_t climbCms,
               float targetAltM, float& xM, float& yM) const {
    xM = sampleXM;
    yM = sampleYM;
    // Add 1 m/s to experienced climb for the glider's approximate sink through the air.
    // Weak/sinking observations have no reliable upward slope: retain their recorded position.
    const int32_t airClimbCms = static_cast<int32_t>(climbCms) + 100;
    if (east == INT8_MIN || north == INT8_MIN || airClimbCms < 100 || !isfinite(targetAltM)) return;

    const float riseTimeS = (targetAltM - sampleAltM) * 100.0f / airClimbCms;
    xM += east * 0.25f * riseTimeS;
    yM += north * 0.25f * riseTimeS;
  }
};

static_assert(sizeof(ThermalSampleWind) == 2, "Captured thermal wind must stay compact");
