#include "navigation/thermal_core.h"

#include <math.h>
#include <string.h>

#include "ui/settings/settings.h"

ThermalCore thermalCore;

namespace {
  constexpr uint8_t MIN_SAMPLES = 8;
  constexpr uint16_t MIN_TURN_DEG = 120;
  constexpr uint8_t BIN_COUNT = 12;
  constexpr uint8_t LOOKBACK_S = 40;
  constexpr float METERS_PER_PX = 1.8f;
  constexpr int16_t AIRCRAFT_Y = 124;
  constexpr int16_t LEFT_AIRCRAFT_X = 20;
  constexpr int16_t RIGHT_AIRCRAFT_X = 76;
  constexpr int16_t CENTER_AIRCRAFT_X = 48;
  constexpr int16_t SCREEN_W = 96;
  constexpr int16_t MAP_TOP = 76;
  constexpr int16_t MAP_SIZE = 96;

  int16_t wrap180(int16_t deg) {
    while (deg > 180) deg -= 360;
    while (deg < -180) deg += 360;
    return deg;
  }

  int16_t headingDelta(int16_t newer, int16_t older) { return wrap180(newer - older); }

  int16_t turnTotalDeg(const ThermalTracker::CoreSample* samples, uint8_t count) {
    int16_t total = 0;
    for (uint8_t i = 1; i < count; ++i) {
      total += headingDelta(samples[i].courseDeg, samples[i - 1].courseDeg);
    }
    return total;
  }

  int8_t directionForTurn(int16_t totalDeg) {
    if (totalDeg > 5) return 1;
    if (totalDeg < -5) return -1;
    return 0;
  }

  ThermalCoreMarkerGlyph glyphForClimb(int16_t climbCms) {
    const int16_t climbStart = settings.vario_climbStart;
    if (climbCms < climbStart) return ThermalCoreMarkerGlyph::Cross3;

    constexpr int16_t MAX_BUCKET_CLIMB_CMS = 500;
    const int16_t span = max<int16_t>(1, MAX_BUCKET_CLIMB_CMS - climbStart);
    const int16_t lightLimit = climbStart + span / 3;
    const int16_t mediumLimit = climbStart + (2 * span) / 3;
    if (climbCms < lightLimit) return ThermalCoreMarkerGlyph::Ring5;
    if (climbCms < mediumLimit) return ThermalCoreMarkerGlyph::Ring7Thick;
    return ThermalCoreMarkerGlyph::Ring9Thick;
  }

  uint8_t ageWeight(uint32_t ageS) {
    if (ageS >= LOOKBACK_S) return 38;  // about 15% of 255
    return static_cast<uint8_t>(255 - (ageS * 217) / LOOKBACK_S);
  }

  void sampleToScreen(const ThermalTracker::CoreSample& sample,
                      const ThermalTracker::CoreSample& latest, int16_t aircraftX, int16_t& x,
                      int16_t& y, float* rightOut = nullptr, float* aheadOut = nullptr) {
    const float heading = latest.courseDeg * DEG_TO_RAD;
    const float dx = sample.xM - latest.xM;
    const float dy = sample.yM - latest.yM;
    const float right = cosf(heading) * dx - sinf(heading) * dy;
    const float ahead = sinf(heading) * dx + cosf(heading) * dy;
    x = static_cast<int16_t>(roundf(aircraftX + right / METERS_PER_PX));
    y = static_cast<int16_t>(roundf(AIRCRAFT_Y - ahead / METERS_PER_PX));
    if (rightOut != nullptr) *rightOut = right;
    if (aheadOut != nullptr) *aheadOut = ahead;
  }
}  // namespace

void ThermalCore::reset() { memset(&estimate_, 0, sizeof(estimate_)); }

void ThermalCore::update() {
  ThermalTracker::CoreSample samples[THERMAL_CORE_MAX_MARKERS];
  const uint8_t sampleCount = thermalTracker.recentCoreSamples(samples, THERMAL_CORE_MAX_MARKERS);
  reset();
  if (sampleCount == 0) return;

  const ThermalTracker::CoreSample& latest = samples[sampleCount - 1];
  const int16_t totalTurn = turnTotalDeg(samples, sampleCount);
  const bool hasSufficientTurn = sampleCount >= MIN_SAMPLES && abs(totalTurn) >= MIN_TURN_DEG;
  const int8_t direction = hasSufficientTurn ? directionForTurn(totalTurn) : 0;
  const int16_t aircraftX = direction < 0   ? RIGHT_AIRCRAFT_X
                            : direction > 0 ? LEFT_AIRCRAFT_X
                                            : CENTER_AIRCRAFT_X;

  estimate_.direction = direction;
  estimate_.markerCount = sampleCount;

  for (uint8_t i = 0; i < sampleCount; ++i) {
    int16_t x = 0;
    int16_t y = 0;
    sampleToScreen(samples[i], latest, aircraftX, x, y);
    ThermalCoreMarker& marker = estimate_.markers[i];
    marker.visible = x >= 0 && x < SCREEN_W && y >= MAP_TOP && y < MAP_TOP + MAP_SIZE;
    marker.x = x;
    marker.y = y;
    marker.glyph = glyphForClimb(samples[i].climbCms);
  }

  if (!hasSufficientTurn || direction == 0) return;

  int32_t binLift[BIN_COUNT] = {};
  uint16_t binWeight[BIN_COUNT] = {};
  int32_t recentLift = 0;
  uint16_t recentWeight = 0;
  int32_t priorLift = 0;
  uint16_t priorWeight = 0;

  for (uint8_t i = 0; i < sampleCount; ++i) {
    const ThermalTracker::CoreSample& sample = samples[i];
    const uint32_t age = latest.timeS > sample.timeS ? latest.timeS - sample.timeS : 0;
    const uint8_t weight = ageWeight(age);

    float right = 0;
    float ahead = 0;
    int16_t unusedX = 0;
    int16_t unusedY = 0;
    sampleToScreen(sample, latest, aircraftX, unusedX, unusedY, &right, &ahead);
    float phase = atan2f(right, ahead) * RAD_TO_DEG;
    if (phase < 0) phase += 360.0f;
    if (direction < 0) phase = 360.0f - phase;
    if (phase >= 360.0f) phase -= 360.0f;
    const uint8_t bin =
        min<uint8_t>(BIN_COUNT - 1, static_cast<uint8_t>(phase / 360.0f * BIN_COUNT));
    binLift[bin] += static_cast<int32_t>(sample.climbCms) * weight;
    binWeight[bin] += weight;

    if (age <= 5) {
      recentLift += static_cast<int32_t>(sample.climbCms) * weight;
      recentWeight += weight;
    } else if (age <= 16) {
      priorLift += static_cast<int32_t>(sample.climbCms) * weight;
      priorWeight += weight;
    }
  }

  const auto avgBin = [&](uint8_t index, int16_t fallback) {
    return binWeight[index] > 0 ? static_cast<int16_t>(binLift[index] / binWeight[index])
                                : fallback;
  };
  const int16_t currentAvg =
      (avgBin(11, latest.climbCms) + avgBin(0, latest.climbCms) + avgBin(1, latest.climbCms)) / 3;
  const int16_t oppositeAvg =
      (avgBin(5, latest.climbCms) + avgBin(6, latest.climbCms) + avgBin(7, latest.climbCms)) / 3;
  const int16_t recentAvg =
      recentWeight > 0 ? static_cast<int16_t>(recentLift / recentWeight) : latest.climbCms;
  const int16_t priorAvg =
      priorWeight > 0 ? static_cast<int16_t>(priorLift / priorWeight) : recentAvg;

  const int16_t symmetry = currentAvg - oppositeAvg;
  const int16_t trend = recentAvg - priorAvg;
  const int16_t adviceCms = constrain((symmetry * 65) / 90 + (trend * 35) / 110, -100, 100);
  estimate_.adviceQ7 = static_cast<int8_t>((adviceCms * 127) / 100);
  estimate_.valid = true;
}
