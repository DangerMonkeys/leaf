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
  constexpr uint32_t FIT_STRAIGHT_BREAK_MS = 3000;
  constexpr uint16_t FIT_REVERSE_BREAK_DEG = 30;
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
                             uint8_t windowSamples, uint32_t segmentStartMs) {
    const uint8_t first = count > windowSamples ? count - windowSamples : 0;
    int16_t total = 0;
    for (uint8_t i = first + 1; i < count; ++i) {
      if (static_cast<int32_t>(samples[i - 1].capturedAtMs - segmentStartMs) < 0) continue;
      total += headingDelta(samples[i].courseDeg, samples[i - 1].courseDeg);
    }
    return total;
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
  lastGuidanceSampleMs_ = 0;
  fitStartMs_ = fitStraightMs_ = fitReverseStartMs_ = 0;
  fitReverseDeg_ = 0;
  fitDirection_ = 0;
  episodeClimbScaleValid_ = false;
  episodeMaxClimbCms_ = 0;
  target_.reset();
}

void ThermalCore::updateTurnSegment(const ThermalTracker::CoreSample& latest,
                                    const ThermalTracker::CoreSample& prior) {
  const uint32_t intervalMs = latest.capturedAtMs - prior.capturedAtMs;
  const int16_t delta = headingDelta(latest.courseDeg, prior.courseDeg);
  if (!latest.valid || !prior.valid || intervalMs < MIN_TURN_SAMPLE_INTERVAL_MS ||
      intervalMs > MAX_TURN_SAMPLE_INTERVAL_MS || abs(delta) > 90) {
    fitDirection_ = 0;
    fitStartMs_ = latest.capturedAtMs;
    fitStraightMs_ = fitReverseDeg_ = 0;
    return;
  }
  if (static_cast<uint32_t>(abs(delta)) * 1000 <= 2 * intervalMs) {
    fitStraightMs_ = min(fitStraightMs_ + intervalMs, FIT_STRAIGHT_BREAK_MS);
    if (fitStraightMs_ >= FIT_STRAIGHT_BREAK_MS) {
      fitDirection_ = 0;
      fitStartMs_ = latest.capturedAtMs;
      fitReverseDeg_ = 0;
    }
    return;
  }
  fitStraightMs_ = 0;
  const int8_t direction = delta > 0 ? 1 : -1;
  if (fitDirection_ == 0) {
    fitDirection_ = direction;
    fitStartMs_ = prior.capturedAtMs;
    fitReverseDeg_ = 0;
  } else if (direction != fitDirection_) {
    if (fitReverseDeg_ == 0) fitReverseStartMs_ = prior.capturedAtMs;
    fitReverseDeg_ += abs(delta);
    if (fitReverseDeg_ >= FIT_REVERSE_BREAK_DEG) {
      fitDirection_ = direction;
      fitStartMs_ = fitReverseStartMs_;
      fitReverseDeg_ = 0;
    }
  } else {
    fitReverseDeg_ = 0;
  }
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
  if (latest.capturedAtMs != lastGuidanceSampleMs_) {
    if (sampleCount >= 2) updateTurnSegment(latest, samples[sampleCount - 2]);
    lastGuidanceSampleMs_ = latest.capturedAtMs;
  }
  const uint8_t turnWindowSamples =
      turnGuidancePreviouslyActive_ ? REENTRY_TURN_SAMPLES : INITIAL_TURN_SAMPLES;
  const uint16_t requiredTurnDeg =
      turnGuidancePreviouslyActive_ ? REENTRY_TURN_DEG : INITIAL_TURN_DEG;
  const int16_t recentTurn =
      recentTurnTotalDeg(samples, sampleCount, turnWindowSamples, fitStartMs_);
  const bool hasSufficientRecentTurn =
      sampleCount >= turnWindowSamples && abs(recentTurn) >= requiredTurnDeg;

  if (fitDirection_ == 0) {
    turnGuidanceActive_ = false;
  } else if (!turnGuidanceActive_ && hasSufficientRecentTurn) {
    turnGuidanceActive_ = true;
    turnGuidancePreviouslyActive_ = true;
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

  const int8_t direction = turnGuidanceActive_ ? fitDirection_ : 0;
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
                                                settings.vario_climbStart, fitStartMs_);
  const bool samplesFresh = nowMs - latest.capturedAtMs <= MAX_TURN_SAMPLE_INTERVAL_MS;
  target_.projectToAltitude(targetAltM);
  if (samplesFresh) target_.observe(center, latest.capturedAtMs);
  target_.updateVisibility(latest.xM, latest.yM, latest.capturedAtMs, samplesFresh);
  estimate_.centerValid = target_.visible() && samplesFresh;
  if (estimate_.centerValid) {
    pointToScreen(target_.eastM(), target_.northM(), mapReference.xM, mapReference.yM,
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
