#pragma once

#include "navigation/thermal_tracker.h"

struct ThermalLiftCenter {
  bool valid = false;
  float eastM = 0;
  float northM = 0;
  float slopeEast = 0;
  float slopeNorth = 0;
};

// Accepts the tracker's non-owning view (or synthetic samples in host tests). No sample copy
// buffer is needed. Integrate by course change so a slow part of a turn does not dominate.
template <typename Samples>
ThermalLiftCenter estimateThermalLiftCenter(const Samples& samples, uint8_t count, int8_t direction,
                                            float altitudeM, int16_t climbStartCms) {
  ThermalLiftCenter result;
  if (count < 12 || direction == 0 || !isfinite(altitudeM)) return result;

  const auto latest = samples[count - 1];
  float sumEast = 0, sumNorth = 0, sumLift = 0;
  float sumSlopeEast = 0, sumSlopeNorth = 0;
  float sumX = 0, sumY = 0, sumXX = 0, sumYY = 0, sumXY = 0, sumAngle = 0;
  float turn = 0;
  int16_t reverseTurn = 0;
  uint8_t usedEdges = 0;

  const auto accumulate = [&](const ThermalTracker::CoreSample& sample, float angleWeight) {
    float east = 0, north = 0, aboveEast = 0, aboveNorth = 0;
    sample.wind.project(sample.xM, sample.yM, sample.altM, sample.climbCms, altitudeM, east, north);
    sample.wind.project(sample.xM, sample.yM, sample.altM, sample.climbCms, altitudeM + 1,
                        aboveEast, aboveNorth);
    // Local coordinates keep the spatial-spread calculation precise far from the GPS origin.
    const float x = east - latest.xM;
    const float y = north - latest.yM;
    const float lift =
        sample.climbCms >= climbStartCms && sample.climbCms > 0 ? sample.climbCms : 0;
    const float weight = angleWeight * lift;
    sumEast += x * weight;
    sumNorth += y * weight;
    sumLift += weight;
    sumSlopeEast += (aboveEast - east) * weight;
    sumSlopeNorth += (aboveNorth - north) * weight;
    sumX += x * angleWeight;
    sumY += y * angleWeight;
    sumXX += x * x * angleWeight;
    sumYY += y * y * angleWeight;
    sumXY += x * y * angleWeight;
    sumAngle += angleWeight;
  };

  for (uint8_t i = count - 1; i > 0 && turn < 360; --i) {
    const auto newer = samples[i];
    const auto older = samples[i - 1];
    const uint32_t intervalMs = newer.capturedAtMs - older.capturedAtMs;
    if (!newer.valid || !older.valid || intervalMs < 250 || intervalMs > 2000 ||
        latest.capturedAtMs - older.capturedAtMs > 40000) {
      return result;
    }
    int16_t delta = newer.courseDeg - older.courseDeg;
    while (delta > 180) delta -= 360;
    while (delta < -180) delta += 360;
    delta *= direction;
    if (abs(delta) > 90) return result;  // reject isolated GPS course jumps
    if (delta < 0) {
      reverseTurn -= delta;
      if (reverseTurn > 30) return result;
      turn += delta;
      continue;
    }
    if (delta == 0) continue;

    // Include only the necessary fraction of the oldest interval to avoid double-counting
    // the start/end of a lap. A trapezoidal weight splits each interval between its endpoints.
    const float angle = min<float>(delta, 360 - turn);
    accumulate(newer, angle * 0.5f);
    accumulate(older, angle * 0.5f);
    turn += angle;
    ++usedEdges;
  }
  if (turn < 360 || usedEdges < 10 || sumAngle <= 0 || sumLift / sumAngle < 20) return result;

  const float meanX = sumX / sumAngle;
  const float meanY = sumY / sumAngle;
  const float varianceX = sumXX / sumAngle - meanX * meanX;
  const float varianceY = sumYY / sumAngle - meanY * meanY;
  const float covariance = sumXY / sumAngle - meanX * meanY;
  const float trace = varianceX + varianceY;
  // Require a genuinely two-dimensional path, rather than a nearly straight traverse.
  if (trace < 50 || varianceX * varianceY - covariance * covariance < trace * trace * 0.02f) {
    return result;
  }

  result.eastM = latest.xM + sumEast / sumLift;
  result.northM = latest.yM + sumNorth / sumLift;
  result.slopeEast = sumSlopeEast / sumLift;
  result.slopeNorth = sumSlopeNorth / sumLift;
  result.valid = isfinite(result.eastM) && isfinite(result.northM);
  return result;
}
