#pragma once

#include "navigation/thermal_tracker.h"

struct ThermalLiftCenter {
  bool valid = false;
  float eastM = 0;
  float northM = 0;
  float slopeEast = 0;
  float slopeNorth = 0;
  uint32_t lapStartMs = 0;
  uint32_t lapEndMs = 0;
  float lapDurationMs = 0;
  float climbIntegralCmsMs = 0;
  float guidanceGain = 1;
};

// Accepts the tracker's non-owning view (or synthetic samples in host tests). No sample copy
// buffer is needed. Integrate by course change so a slow part of a turn does not dominate.
template <typename Samples>
ThermalLiftCenter estimateThermalLiftCenter(const Samples& samples, uint8_t count, int8_t direction,
                                            float altitudeM, int16_t climbStartCms,
                                            uint32_t segmentStartMs = 0) {
  ThermalLiftCenter result;
  if (count < 12 || direction == 0 || !isfinite(altitudeM)) return result;

  const auto latest = samples[count - 1];
  float sumEast = 0, sumNorth = 0, sumLift = 0;
  float sumSlopeEast = 0, sumSlopeNorth = 0;
  float sumPathSlopeEast = 0, sumPathSlopeNorth = 0, sumLiftSquared = 0;
  float sumX = 0, sumY = 0, sumXX = 0, sumYY = 0, sumXY = 0, sumAngle = 0;
  float turn = 0;
  int16_t reverseTurn = 0;
  uint8_t usedEdges = 0;
  float lapDurationMs = 0, climbIntegralCmsMs = 0;
  uint32_t lapStartMs = latest.capturedAtMs;
  uint8_t oldestEdge = 0;
  float oldestAngle = 0;

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
    sumPathSlopeEast += (aboveEast - east) * angleWeight;
    sumPathSlopeNorth += (aboveNorth - north) * angleWeight;
    sumLiftSquared += lift * lift * angleWeight;
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
        latest.capturedAtMs - older.capturedAtMs > 40000 ||
        (segmentStartMs != 0 && static_cast<int32_t>(older.capturedAtMs - segmentStartMs) < 0)) {
      return result;
    }
    int16_t delta = newer.courseDeg - older.courseDeg;
    while (delta > 180) delta -= 360;
    while (delta < -180) delta += 360;
    delta *= direction;
    if (abs(delta) > 90) return result;  // reject isolated GPS course jumps
    // Integrate signed net climb over time, including sinking and zero-course-change intervals.
    // Interpolate the oldest endpoint when only part of its interval completes the lap.
    const float fraction = delta > 0 ? min<float>(1, (360 - turn) / delta) : 1;
    const float durationMs = intervalMs * fraction;
    const float boundaryClimb = newer.climbCms + (older.climbCms - newer.climbCms) * fraction;
    lapDurationMs += durationMs;
    climbIntegralCmsMs += (newer.climbCms + boundaryClimb) * 0.5f * durationMs;
    lapStartMs = newer.capturedAtMs - static_cast<uint32_t>(roundf(durationMs));
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
    oldestEdge = i;
    oldestAngle = angle;
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

  const float offsetX = sumEast / sumLift - meanX;
  const float offsetY = sumNorth / sumLift - meanY;
  const float offsetSquared = offsetX * offsetX + offsetY * offsetY;
  const float meanLift = sumLift / sumAngle;
  const float liftVariance = max(0.0f, sumLiftSquared / sumAngle - meanLift * meanLift);
  const float covXLift = sumEast / sumAngle - meanX * meanLift;
  const float covYLift = sumNorth / sumAngle - meanY * meanLift;
  const float determinant = varianceX * varianceY - covariance * covariance;
  // How well a spatial slope explains the lift variation. A broad strong side fits a slope;
  // one isolated spike or alternating noise does not, so it must not get extra spatial gain.
  const float explainedVariance =
      constrain((varianceY * covXLift * covXLift - 2 * covariance * covXLift * covYLift +
                 varianceX * covYLift * covYLift) /
                    determinant,
                0.0f, liftVariance);
  const float coherence = liftVariance > 0 ? explainedVariance / liftVariance : 0;
  const float contrastCms = 2 * sqrtf(2 * explainedVariance);
  const float coherentBlend = constrain((coherence - 0.4f) / 0.4f, 0.0f, 1.0f);
  // Ignore differences below 0.3 m/s; reach full gain for a coherent 1 m/s difference.
  const float contrastBlend = constrain((contrastCms - 30) / 70, 0.0f, 1.0f);
  float gain = 1;
  if (coherentBlend > 0 && contrastBlend > 0 && offsetSquared > 0.0001f) {
    // Revisit the accepted lap without copying the tracker's breadcrumb buffer.
    const auto visitLap = [&](const auto& visitor) {
      for (uint8_t i = oldestEdge; i < count; ++i) {
        const auto newer = samples[i];
        const auto older = samples[i - 1];
        int16_t delta = newer.courseDeg - older.courseDeg;
        while (delta > 180) delta -= 360;
        while (delta < -180) delta += 360;
        delta *= direction;
        if (delta <= 0) continue;
        const float weight = (i == oldestEdge ? oldestAngle : delta) * 0.5f;
        visitor(newer, weight);
        visitor(older, weight);
      }
    };
    const auto projected = [&](const ThermalTracker::CoreSample& sample, float& x, float& y) {
      sample.wind.project(sample.xM, sample.yM, sample.altM, sample.climbCms, altitudeM, x, y);
      x -= latest.xM;
      y -= latest.yM;
    };
    const float strongThreshold = meanLift + 0.5f * sqrtf(liftVariance);
    float strongX = 0, strongY = 0, strongLift = 0, strongWeight = 0;
    visitLap([&](const ThermalTracker::CoreSample& sample, float weight) {
      if (sample.climbCms < strongThreshold) return;
      float x, y;
      projected(sample, x, y);
      strongX += x * weight;
      strongY += y * weight;
      strongLift += sample.climbCms * weight;
      strongWeight += weight;
    });

    bool bracketed = false;
    if (strongWeight > 0) {
      strongX /= strongWeight;
      strongY /= strongWeight;
      strongLift /= strongWeight;
      uint8_t weakerSamples[8] = {};
      // The full recent observation window can bracket a peak even when the latest orbit alone
      // cannot. Use only this segment, project every sample to today's altitude, and require
      // multiple weaker observations in each supported direction around the strong patch.
      for (uint8_t i = 0; i < count; ++i) {
        const auto sample = samples[i];
        if (!sample.valid || latest.capturedAtMs - sample.capturedAtMs > 40000 ||
            (segmentStartMs != 0 &&
             static_cast<int32_t>(sample.capturedAtMs - segmentStartMs) < 0) ||
            sample.climbCms > strongLift - max(30.0f, contrastCms * 0.25f))
          continue;
        float x, y;
        projected(sample, x, y);
        x -= strongX;
        y -= strongY;
        if (x * x + y * y < 9) continue;
        float angle = atan2f(y, x);
        if (angle < 0) angle += 2 * PI;
        const uint8_t sector = min<uint8_t>(7, static_cast<uint8_t>(angle * 4 / PI));
        if (weakerSamples[sector] < 2) ++weakerSamples[sector];
      }
      uint8_t mask = 0, sectors = 0;
      for (uint8_t i = 0; i < 8; ++i) {
        if (weakerSamples[i] >= 2) {
          mask |= 1 << i;
          ++sectors;
        }
      }
      bracketed = sectors >= 3;
      for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t arc = (1 << i) | (1 << ((i + 1) % 8)) | (1 << ((i + 2) % 8));
        if (!(mask & arc)) bracketed = false;  // an open 135-degree arc is not surrounding evidence
      }
    }
    if (!bracketed) {
      gain = 1 + 3 * coherentBlend * contrastBlend;
      const float distance = sqrtf(offsetSquared);
      const float ux = offsetX / distance, uy = offsetY / distance;
      // One orbit radius in the push direction also accommodates oval/uneven circles.
      float radius =
          sqrtf(2 * (varianceX * ux * ux + 2 * covariance * ux * uy + varianceY * uy * uy));
      float sampledExtent = 0;
      visitLap([&](const ThermalTracker::CoreSample& sample, float) {
        float x, y;
        projected(sample, x, y);
        sampledExtent = max(sampledExtent, (x - meanX) * ux + (y - meanY) * uy);
      });
      radius = min(radius, sampledExtent);
      gain = min(gain, max(1.0f, radius / distance));
    }
  }

  result.eastM = latest.xM + meanX + gain * offsetX;
  result.northM = latest.yM + meanY + gain * offsetY;
  // Transport the amplified position with the same combination of geometric/lift-weighted
  // slopes, rather than leaving the retained target on the original centroid's slant.
  const float pathSlopeEast = sumPathSlopeEast / sumAngle;
  const float pathSlopeNorth = sumPathSlopeNorth / sumAngle;
  result.slopeEast = pathSlopeEast + gain * (sumSlopeEast / sumLift - pathSlopeEast);
  result.slopeNorth = pathSlopeNorth + gain * (sumSlopeNorth / sumLift - pathSlopeNorth);
  result.guidanceGain = gain;
  result.lapStartMs = lapStartMs;
  result.lapEndMs = latest.capturedAtMs;
  result.lapDurationMs = lapDurationMs;
  result.climbIntegralCmsMs = climbIntegralCmsMs;
  result.valid = isfinite(result.eastM) && isfinite(result.northM);
  return result;
}
