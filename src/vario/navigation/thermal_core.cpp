#include "navigation/thermal_core.h"

#include <math.h>
#include "instruments/baro.h"
#include "navigation/thermal_center.h"
#include "ui/settings/settings.h"

ThermalCore thermalCore;

namespace {
  constexpr uint8_t INITIAL_TURN_SAMPLES = 8;
  constexpr uint16_t INITIAL_TURN_DEG = 120;
  constexpr uint8_t REENTRY_TURN_SAMPLES = 10;
  constexpr uint16_t REENTRY_TURN_DEG = 90;
  constexpr uint32_t TURN_EXIT_COURSE_WINDOW_MS = 7000;
  constexpr uint32_t TURN_EXIT_MIN_COURSE_WINDOW_MS = 6000;
  // Over this window, roughly 2 degrees/second in either direction counts as a gentle turn.
  constexpr uint16_t TURN_EXIT_MAX_COURSE_DEVIATION_DEG = 10;
  constexpr uint32_t TURN_EXIT_STRAIGHT_MS = 15000;
  constexpr uint32_t TURN_EXIT_TURNING_MS = 30000;
  constexpr uint8_t GUIDANCE_BIN_COUNT = 12;
  constexpr uint8_t LOOKBACK_S = 40;
  constexpr int16_t MAP_CENTER_Y = 124;
  constexpr int16_t LEFT_AIRCRAFT_X = 20;
  constexpr int16_t RIGHT_AIRCRAFT_X = 76;
  constexpr int16_t CENTER_AIRCRAFT_X = 48;
  constexpr int16_t SCREEN_W = 96;
  constexpr int16_t MAP_TOP = 71;
  constexpr int16_t MAP_HEIGHT = 104;
  constexpr uint32_t MAX_MAP_EXTRAPOLATION_MS = 1000;
  constexpr uint32_t MIN_TURN_SAMPLE_INTERVAL_MS = 250;
  constexpr uint32_t MAX_TURN_SAMPLE_INTERVAL_MS = 2000;
  constexpr uint16_t MIN_TURN_EXTRAPOLATION_SPEED_CMS = 200;
  constexpr float MAX_TURN_RATE_DPS = 90.0f;

  struct MapReference {
    float xM = 0;
    float yM = 0;
    float courseDeg = 0;
  };

  int16_t wrap180(int16_t deg) {
    while (deg > 180) deg -= 360;
    while (deg < -180) deg += 360;
    return deg;
  }

  int16_t headingDelta(int16_t newer, int16_t older) { return wrap180(newer - older); }

  int16_t recentTurnTotalDeg(const ThermalTracker::CoreSamples& samples, uint8_t count,
                             uint8_t windowSamples) {
    const uint8_t first = count > windowSamples ? count - windowSamples : 0;
    int16_t total = 0;
    for (uint8_t i = first + 1; i < count; ++i) {
      total += headingDelta(samples[i].courseDeg, samples[i - 1].courseDeg);
    }
    return total;
  }

  bool recentCourseWindowIsStraight(const ThermalTracker::CoreSamples& samples, uint8_t count,
                                    uint32_t& windowSpanMs) {
    windowSpanMs = 0;
    if (count < 2) return false;

    const uint32_t latestMs = samples[count - 1].capturedAtMs;
    uint8_t first = count - 1;
    while (first > 0 && latestMs - samples[first - 1].capturedAtMs <= TURN_EXIT_COURSE_WINDOW_MS) {
      first--;
    }

    windowSpanMs = latestMs - samples[first].capturedAtMs;
    const uint8_t pointCount = count - first;
    if (windowSpanMs < TURN_EXIT_MIN_COURSE_WINDOW_MS || pointCount < 5) return false;

    // Unwrap the course through the window, then measure its angular envelope. Discarding one
    // value from each extreme makes the result insensitive to one isolated GPS course jump.
    int32_t unwrappedDeg = 0;
    int32_t lowest = 0;
    int32_t secondLowest = INT32_MAX;
    int32_t highest = 0;
    int32_t secondHighest = INT32_MIN;
    for (uint8_t i = first + 1; i < count; ++i) {
      unwrappedDeg += headingDelta(samples[i].courseDeg, samples[i - 1].courseDeg);
      if (unwrappedDeg < lowest) {
        secondLowest = lowest;
        lowest = unwrappedDeg;
      } else if (unwrappedDeg < secondLowest) {
        secondLowest = unwrappedDeg;
      }
      if (unwrappedDeg > highest) {
        secondHighest = highest;
        highest = unwrappedDeg;
      } else if (unwrappedDeg > secondHighest) {
        secondHighest = unwrappedDeg;
      }
    }

    const int32_t robustLowest = secondLowest == INT32_MAX ? lowest : secondLowest;
    const int32_t robustHighest = secondHighest == INT32_MIN ? highest : secondHighest;
    return robustHighest - robustLowest <= TURN_EXIT_MAX_COURSE_DEVIATION_DEG;
  }

  int8_t directionForTurn(int16_t totalDeg) {
    if (totalDeg > 5) return 1;
    if (totalDeg < -5) return -1;
    return 0;
  }

  constexpr int16_t DEFAULT_BREADCRUMB_SCALE_CMS = 350;

  ThermalCoreMarkerGlyph glyphForClimb(int16_t climbCms, int16_t episodeMaxClimbCms) {
    const int16_t climbStart = settings.vario_climbStart;
    if (climbCms < climbStart) return ThermalCoreMarkerGlyph::Cross3;

    // Use 0.75 m/s bands through 3 m/s, then the filled ring. Above a 3.5 m/s episode peak,
    // expand the boundaries proportionally. Multiplying by 14 keeps the initial bands unchanged.
    const int32_t scaleCms = max<int16_t>(DEFAULT_BREADCRUMB_SCALE_CMS, episodeMaxClimbCms);
    const int32_t scaledClimb = static_cast<int32_t>(climbCms) * 14;
    if (scaledClimb <= scaleCms * 3) return ThermalCoreMarkerGlyph::Ring5;
    if (scaledClimb <= scaleCms * 6) return ThermalCoreMarkerGlyph::Ring7;
    if (scaledClimb <= scaleCms * 9) return ThermalCoreMarkerGlyph::Ring9;
    if (scaledClimb <= scaleCms * 12) return ThermalCoreMarkerGlyph::Ring11;
    return ThermalCoreMarkerGlyph::FilledRing11;
  }

  int16_t maxClimb(const ThermalTracker::CoreSamples& samples, uint8_t count) {
    int16_t result = settings.vario_climbStart;
    for (uint8_t i = 0; i < count; ++i) result = max(result, samples[i].climbCms);
    return result;
  }

  uint8_t ageWeight(uint32_t ageS) {
    if (ageS >= LOOKBACK_S) return 38;  // about 15% of 255
    return static_cast<uint8_t>(255 - (ageS * 217) / LOOKBACK_S);
  }

  void pointToScreen(float pointXM, float pointYM, float referenceXM, float referenceYM,
                     float referenceCourseDeg, int16_t aircraftX, int16_t& x, int16_t& y,
                     float* rightOut = nullptr, float* aheadOut = nullptr) {
    const float heading = referenceCourseDeg * DEG_TO_RAD;
    const float dx = pointXM - referenceXM;
    const float dy = pointYM - referenceYM;
    const float right = cosf(heading) * dx - sinf(heading) * dy;
    const float ahead = sinf(heading) * dx + cosf(heading) * dy;
    // Large altitude changes can project far beyond this small map. Saturate before narrowing
    // so an off-screen projection cannot wrap back into the visible screen.
    x = static_cast<int16_t>(constrain(roundf(aircraftX + right / ThermalCore::MAP_METERS_PER_PX),
                                       static_cast<float>(INT16_MIN),
                                       static_cast<float>(INT16_MAX)));
    y = static_cast<int16_t>(
        constrain(roundf(MAP_CENTER_Y - ahead / ThermalCore::MAP_METERS_PER_PX),
                  static_cast<float>(INT16_MIN), static_cast<float>(INT16_MAX)));
    if (rightOut != nullptr) *rightOut = right;
    if (aheadOut != nullptr) *aheadOut = ahead;
  }

  void sampleToScreen(const ThermalTracker::CoreSample& sample,
                      const ThermalTracker::CoreSample& latest, float targetAltM, int16_t aircraftX,
                      int16_t& x, int16_t& y, float* rightOut = nullptr,
                      float* aheadOut = nullptr) {
    float projectedXM = 0;
    float projectedYM = 0;
    sample.wind.project(sample.xM, sample.yM, sample.altM, sample.climbCms, targetAltM, projectedXM,
                        projectedYM);
    pointToScreen(projectedXM, projectedYM, latest.xM, latest.yM, latest.courseDeg, aircraftX, x, y,
                  rightOut, aheadOut);
  }

  float anticipatedTurnRate(const ThermalTracker::CoreSamples& samples, uint8_t sampleCount) {
    if (sampleCount < 2) return 0;

    const ThermalTracker::CoreSample& latest = samples[sampleCount - 1];
    const ThermalTracker::CoreSample& prior = samples[sampleCount - 2];
    if (latest.speedCms < MIN_TURN_EXTRAPOLATION_SPEED_CMS) return 0;

    const uint32_t intervalMs = latest.capturedAtMs - prior.capturedAtMs;
    if (intervalMs < MIN_TURN_SAMPLE_INTERVAL_MS || intervalMs > MAX_TURN_SAMPLE_INTERVAL_MS) {
      return 0;
    }

    const float turnRate = headingDelta(latest.courseDeg, prior.courseDeg) * 1000.0f / intervalMs;
    return constrain(turnRate, -MAX_TURN_RATE_DPS, MAX_TURN_RATE_DPS);
  }

  MapReference extrapolatedMapReference(const ThermalTracker::CoreSamples& samples,
                                        uint8_t sampleCount, uint32_t nowMs) {
    const ThermalTracker::CoreSample& latest = samples[sampleCount - 1];
    const uint32_t elapsedMs = nowMs - latest.capturedAtMs;
    const uint32_t extrapolationMs = min(elapsedMs, MAX_MAP_EXTRAPOLATION_MS);
    const float extrapolationS = extrapolationMs / 1000.0f;
    const float turnRateDps = anticipatedTurnRate(samples, sampleCount);
    const float projectedCourseDeg = latest.courseDeg + turnRateDps * extrapolationS;
    const float midpointCourseRad =
        (latest.courseDeg + turnRateDps * extrapolationS * 0.5f) * DEG_TO_RAD;
    const float distanceM = latest.speedCms * extrapolationMs / 100000.0f;
    return {latest.xM + sinf(midpointCourseRad) * distanceM,
            latest.yM + cosf(midpointCourseRad) * distanceM, projectedCourseDeg};
  }

}  // namespace

void ThermalCore::reset() {
  estimate_ = {};
  turnGuidanceActive_ = false;
  turnGuidancePreviouslyActive_ = false;
  activeTurnDirection_ = 0;
  straightDurationMs_ = 0;
  lastGuidanceSampleMs_ = 0;
  lastSustainedTurnMs_ = 0;
  episodeClimbScaleValid_ = false;
  episodeMaxClimbCms_ = 0;
  centerValid_ = false;
  centerEastM_ = centerNorthM_ = centerAltitudeM_ = 0;
  centerSlopeEast_ = centerSlopeNorth_ = 0;
  lastCenterSampleMs_ = 0;
}

void ThermalCore::update() {
  const auto samples = thermalTracker.recentCoreSamples();
  const uint8_t sampleCount = samples.size();
  estimate_ = {};
  if (sampleCount == 0) {
    reset();
    return;
  }

  const ThermalTracker::CoreSample& latest = samples[sampleCount - 1];
  const uint32_t nowMs = millis();
  const bool guidanceWasActive = turnGuidanceActive_;
  const uint8_t turnWindowSamples =
      turnGuidancePreviouslyActive_ ? REENTRY_TURN_SAMPLES : INITIAL_TURN_SAMPLES;
  const uint16_t requiredTurnDeg =
      turnGuidancePreviouslyActive_ ? REENTRY_TURN_DEG : INITIAL_TURN_DEG;
  const int16_t recentTurn = recentTurnTotalDeg(samples, sampleCount, turnWindowSamples);
  const bool hasSufficientRecentTurn =
      sampleCount >= turnWindowSamples && abs(recentTurn) >= requiredTurnDeg;

  if (latest.capturedAtMs != lastGuidanceSampleMs_) {
    uint32_t courseWindowSpanMs = 0;
    if (recentCourseWindowIsStraight(samples, sampleCount, courseWindowSpanMs)) {
      const uint32_t newSampleMs =
          lastGuidanceSampleMs_ == 0 ? 0 : latest.capturedAtMs - lastGuidanceSampleMs_;
      straightDurationMs_ = straightDurationMs_ == 0
                                ? courseWindowSpanMs
                                : min(straightDurationMs_ + newSampleMs, TURN_EXIT_STRAIGHT_MS);
    } else {
      straightDurationMs_ = 0;
    }
    lastGuidanceSampleMs_ = latest.capturedAtMs;
  }

  if (turnGuidanceActive_) {
    if (hasSufficientRecentTurn) lastSustainedTurnMs_ = latest.capturedAtMs;
    const bool straightExit = straightDurationMs_ >= TURN_EXIT_STRAIGHT_MS;
    const bool turningExit = nowMs - lastSustainedTurnMs_ >= TURN_EXIT_TURNING_MS;
    if (straightExit || turningExit) {
      turnGuidanceActive_ = false;
      activeTurnDirection_ = 0;
    } else {
      const int8_t recentDirection = directionForTurn(recentTurn);
      if (recentDirection != 0) activeTurnDirection_ = recentDirection;
    }
  } else if (straightDurationMs_ == 0 && hasSufficientRecentTurn) {
    turnGuidanceActive_ = true;
    turnGuidancePreviouslyActive_ = true;
    activeTurnDirection_ = directionForTurn(recentTurn);
    lastSustainedTurnMs_ = latest.capturedAtMs;
  }

  const int16_t recentMaxClimbCms = maxClimb(samples, sampleCount);
  if (turnGuidanceActive_) {
    if (!guidanceWasActive) {
      episodeMaxClimbCms_ = recentMaxClimbCms;
    } else {
      episodeMaxClimbCms_ = max(episodeMaxClimbCms_, latest.climbCms);
    }
    episodeClimbScaleValid_ = true;
  } else if (guidanceWasActive) {
    episodeClimbScaleValid_ = false;
    episodeMaxClimbCms_ = 0;
  }
  const int16_t breadcrumbMaxClimbCms =
      episodeClimbScaleValid_ ? episodeMaxClimbCms_ : DEFAULT_BREADCRUMB_SCALE_CMS;

  const int8_t direction = turnGuidanceActive_ ? activeTurnDirection_ : 0;
  const int16_t aircraftX = direction < 0   ? RIGHT_AIRCRAFT_X
                            : direction > 0 ? LEFT_AIRCRAFT_X
                                            : CENTER_AIRCRAFT_X;

  estimate_.direction = direction;
  estimate_.markerCount = sampleCount;

  // The tracker deliberately records one sample per second. Project the aircraft position and
  // ground-track bearing forward for the intervening display refresh so the map moves at 2 Hz.
  // Guidance keeps the latest actual 1 Hz aircraft reference, with the same altitude projection.
  const MapReference mapReference = extrapolatedMapReference(samples, sampleCount, nowMs);
  const float targetAltM = baro.altAdjusted() / 100.0f;
  estimate_.mapCourseDeciDeg = static_cast<int16_t>(roundf(mapReference.courseDeg * 10));

  const auto center = estimateThermalLiftCenter(samples, sampleCount, direction, targetAltM,
                                                settings.vario_climbStart);
  const bool samplesFresh = nowMs - latest.capturedAtMs <= MAX_TURN_SAMPLE_INTERVAL_MS;
  if (centerValid_) {
    // Keep a retained center at the pilot's current altitude while a reversal interrupts the fit.
    centerEastM_ += centerSlopeEast_ * (targetAltM - centerAltitudeM_);
    centerNorthM_ += centerSlopeNorth_ * (targetAltM - centerAltitudeM_);
    centerAltitudeM_ = targetAltM;
  }
  if (center.valid && samplesFresh) {
    if (!centerValid_) {
      centerEastM_ = center.eastM;
      centerNorthM_ = center.northM;
    } else {
      if (latest.capturedAtMs != lastCenterSampleMs_) {
        centerEastM_ += (center.eastM - centerEastM_) * 0.35f;
        centerNorthM_ += (center.northM - centerNorthM_) * 0.35f;
      }
    }
    centerAltitudeM_ = targetAltM;
    centerSlopeEast_ = center.slopeEast;
    centerSlopeNorth_ = center.slopeNorth;
    lastCenterSampleMs_ = latest.capturedAtMs;
    centerValid_ = true;
  } else if (!turnGuidanceActive_ || nowMs - lastCenterSampleMs_ >= TURN_EXIT_TURNING_MS) {
    centerValid_ = false;
  }
  // A missing lap fit gets a grace period; missing GPS still hides the map target immediately.
  estimate_.centerValid = centerValid_ && samplesFresh;
  if (estimate_.centerValid) {
    pointToScreen(centerEastM_, centerNorthM_, mapReference.xM, mapReference.yM,
                  mapReference.courseDeg, aircraftX, estimate_.centerX, estimate_.centerY);
  }

  for (uint8_t i = 0; i < sampleCount; ++i) {
    // Project lift to the pilot's current altitude along the slope captured at observation.
    // Elapsed time alone does not move a source-fed plume's lift at a fixed altitude.
    const auto sample = samples[i];
    float breadcrumbXM = 0;
    float breadcrumbYM = 0;
    sample.wind.project(sample.xM, sample.yM, sample.altM, sample.climbCms, targetAltM,
                        breadcrumbXM, breadcrumbYM);
    int16_t x = 0;
    int16_t y = 0;
    pointToScreen(breadcrumbXM, breadcrumbYM, mapReference.xM, mapReference.yM,
                  mapReference.courseDeg, aircraftX, x, y);
    ThermalCoreMarker& marker = estimate_.markers[i];
    marker.visible = x >= 0 && x < SCREEN_W && y >= MAP_TOP && y < MAP_TOP + MAP_HEIGHT;
    marker.x = x;
    marker.y = y;
    marker.glyph = glyphForClimb(sample.climbCms, breadcrumbMaxClimbCms);
  }

  if (!turnGuidanceActive_ || direction == 0) return;

  int32_t binLift[GUIDANCE_BIN_COUNT] = {};
  uint16_t binWeight[GUIDANCE_BIN_COUNT] = {};
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
    sampleToScreen(sample, latest, targetAltM, aircraftX, unusedX, unusedY, &right, &ahead);
    float phase = atan2f(right, ahead) * RAD_TO_DEG;
    if (phase < 0) phase += 360.0f;
    if (direction < 0) phase = 360.0f - phase;
    if (phase >= 360.0f) phase -= 360.0f;
    const uint8_t bin = min<uint8_t>(GUIDANCE_BIN_COUNT - 1,
                                     static_cast<uint8_t>(phase / 360.0f * GUIDANCE_BIN_COUNT));
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
