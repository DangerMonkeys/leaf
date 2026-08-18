#include "navigation/thermal_core.h"

#include <math.h>
#include <string.h>

#include "instruments/baro.h"
#include "instruments/gps.h"
#include "navigation/thermal_tracker.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

ThermalCore thermalCore;

using thermal::Vec2;

namespace {
  constexpr float DEG_TO_RAD_F = 0.0174532925f;
  constexpr float RAD_TO_DEG_F = 57.2957795f;

  inline float clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

  // Bearing "east of North", matching thermal_core_solver.
  inline float bearingOf(const Vec2& v) { return atan2f(v.x, v.y); }

  inline Vec2 fromBearing(float bearingRad, float lengthM) {
    return Vec2{sinf(bearingRad) * lengthM, cosf(bearingRad) * lengthM};
  }

  // Rotate a local east/north offset into the glider's frame: x to the right of the nose, y
  // ahead of it.
  inline void toBodyFrame(const Vec2& v, float headingRad, float& rightM, float& aheadM) {
    const float s = sinf(headingRad);
    const float c = cosf(headingRad);
    rightM = v.x * c - v.y * s;
    aheadM = v.x * s + v.y * c;
  }

  // Exponential smoothing coefficient for a step of dt against a time constant of tau.
  inline float smoothing(float dtS, float tauS) {
    if (dtS <= 0) return 0;
    if (dtS >= 4.0f * tauS) return 1.0f;
    return 1.0f - expf(-dtS / tauS);
  }
}  // namespace

void ThermalCore::reset() {
  solver_.reset();
  memset(&state_, 0, sizeof(state_));
  state_.mapScaleRadiusM = GLIDE_SCALE_RADIUS_M;
  hasLastFix_ = false;
  lastFixMillis_ = 0;
  lastFixPosM_ = Vec2{};
  groundVelMps_ = Vec2{};
  windToMps_ = Vec2{};
  windValid_ = false;
  hasLastUpdate_ = false;
  lastUpdateS_ = 0;
  turnHoldS_ = 0;
  straightHoldS_ = 0;
  hasSmoothedCore_ = false;
  smoothedCoreM_ = Vec2{};
  smoothedCoreS_ = 0;
}

// Reads the sensors, feeds the solver one observation per GPS fix, and reports where the glider
// is and which way it is pointing right now.  "Right now" is dead reckoned from the last fix,
// because the page redraws twice as often as the receiver reports and a trail that jumps once a
// second is much harder to read than one that slides.
bool ThermalCore::sampleSensors(float& nowS, Vec2& groundPosM, float& headingRad) {
  GPSPositionSnapshot fix;
  if (!gps.lastValidFix(fix) || !thermalTracker.localFrameValid()) return false;

  // The air-frame correction multiplies the wind by up to a full window of age, so a wind estimate
  // that is briefly wild while it reconverges would throw every trail point hundreds of metres off
  // the map at once.  Past a speed no paraglider is thermalling in, the ground frame is safer.
  const WindEstimate& wind = windEstimator.getWindEstimate();
  windValid_ = wind.validEstimate && wind.windSpeed <= MAX_USABLE_WIND_MPS;
  windToMps_ = windValid_ ? fromBearing(wind.windDirectionTrue, wind.windSpeed) : Vec2{};
  solver_.setWind(windToMps_, windValid_);

  const uint32_t nowMs = millis();
  nowS = nowMs / 1000.0f;

  const bool isNewFix = !hasLastFix_ || fix.latitude != lastFixLat_ || fix.longitude != lastFixLon_;
  if (isNewFix) {
    float x = 0;
    float y = 0;
    if (!thermalTracker.toLocalMeters(fix.latitude, fix.longitude, x, y)) return false;
    lastFixPosM_ = Vec2{x, y};
    lastFixMillis_ = nowMs;
    lastFixLat_ = fix.latitude;
    lastFixLon_ = fix.longitude;
    hasLastFix_ = true;
    groundVelMps_ = fromBearing(fix.courseDeg * DEG_TO_RAD_F, fix.speedMps);

    if (baro.climbRate1SecAverageValid()) {
      solver_.addObservation(nowS, lastFixPosM_, baro.climbRate1SecAverage() / 100.0f);
    }
  }
  if (!hasLastFix_) return false;

  // Never extrapolate further than one missed fix: past that the receiver has a problem and a
  // frozen trail is more honest than an invented one.
  const float sinceFixS = clampF((nowMs - lastFixMillis_) / 1000.0f, 0.0f, 2.0f);
  groundPosM = lastFixPosM_ + groundVelMps_ * sinceFixS;

  // Track up means nose up, and the nose points along the air heading, not the ground track.
  // In a 2 m/s wind the two differ by a drift angle that swings through a dozen degrees each
  // lap, which would rock the whole picture back and forth once a circle.
  const Vec2 airVel = windValid_ ? groundVelMps_ - windToMps_ : groundVelMps_;
  headingRad = thermal::lengthOf(airVel) > 1.0f ? bearingOf(airVel)
                                                : fix.courseDeg * DEG_TO_RAD_F;
  return true;
}

void ThermalCore::updateCircling(const thermal::CircleFit& fit, float dtS) {
  const float turnRate = fabsf(fit.turnRateDegS);

  if (turnRate >= ENTER_TURN_RATE_DEG_S) {
    turnHoldS_ += dtS;
    straightHoldS_ = 0;
  } else if (turnRate <= LEAVE_TURN_RATE_DEG_S) {
    straightHoldS_ += dtS;
    turnHoldS_ = 0;
  }

  if (!state_.circling) {
    if (turnHoldS_ >= ENTER_HOLD_S && fabsf(fit.sweepDeg) >= ENTER_SWEEP_DEG && fit.valid) {
      state_.circling = true;
      state_.turnDir = fit.sweepDeg > 0 ? 1 : -1;
    }
  } else if (straightHoldS_ >= LEAVE_HOLD_S) {
    state_.circling = false;
    state_.turnDir = 0;
    hasSmoothedCore_ = false;
  } else if (fit.valid && fabsf(fit.sweepDeg) >= ENTER_SWEEP_DEG) {
    // A pilot who reverses the turn gets the layout mirrored, but only once the new direction
    // has covered enough ground to be sure of it.
    state_.turnDir = fit.sweepDeg > 0 ? 1 : -1;
  }

  state_.turnRadiusM = state_.circling && fit.valid ? fit.radiusM : 0;

  const float targetScale =
      state_.turnRadiusM > 0 ? clampF(state_.turnRadiusM, 15.0f, 90.0f) : GLIDE_SCALE_RADIUS_M;
  if (state_.mapScaleRadiusM <= 0) {
    state_.mapScaleRadiusM = targetScale;
  } else {
    state_.mapScaleRadiusM +=
        (targetScale - state_.mapScaleRadiusM) * smoothing(dtS, SCALE_SMOOTH_TAU_S);
  }
}

void ThermalCore::updateCore(const thermal::CoreSolution& solution, float nowS, Vec2 groundPosM,
                             float dtS) {
  const bool wasValid = state_.coreValid;
  state_.coreValid = false;
  state_.centred = false;
  state_.openNow = false;
  state_.confidence = solution.confidence;
  state_.coreOffsetM = 0;

  if (!state_.circling || !solution.valid) {
    hasSmoothedCore_ = false;
    return;
  }

  state_.coreOffsetM = solution.offsetM;

  const uint8_t threshold = wasValid ? KEEP_CONFIDENCE : SHOW_CONFIDENCE;
  if (solution.confidence < threshold) {
    hasSmoothedCore_ = false;
    return;
  }

  // Smooth in the ground frame with the estimate carried downwind between updates, so the
  // marker sits still over the air rather than being dragged back by its own history.
  const Vec2 fresh = groundPosM + solution.coreM;
  if (!hasSmoothedCore_) {
    smoothedCoreM_ = fresh;
    hasSmoothedCore_ = true;
  } else {
    const float driftS = nowS - smoothedCoreS_;
    if (driftS > 0 && windValid_) smoothedCoreM_ = smoothedCoreM_ + windToMps_ * driftS;
    smoothedCoreM_ =
        smoothedCoreM_ + (fresh - smoothedCoreM_) * smoothing(dtS, CORE_SMOOTH_TAU_S);
  }
  smoothedCoreS_ = nowS;
  state_.coreValid = true;
}

void ThermalCore::buildTrail(float nowS, Vec2 groundPosM, float headingRad,
                             const thermal::CoreSolution& solution) {
  state_.trailCount = 0;

  // Everything in the window is drawn.  The fit trims itself back to the stretch that is
  // actually a circle, but the trail must not: that cut moves whenever a fix comes in ragged or
  // the pilot flattens for a second, and tying the drawn history to it makes the entire trail
  // blink out and back for a tick at a time.  What the fit decides only changes how the dots are
  // banded, never whether they exist.
  const bool relative = state_.circling && solution.valid;
  const float mean = solution.meanClimbMps;
  // The band width that makes the shape of a lap readable: a quarter of the swing the lap
  // actually has, floored so a flat lap does not amplify its own noise into a pattern.
  const float span = relative ? fmaxf(1.00f, solution.amplitudeMps) : 0;
  const float climbStartMps = settings.vario_climbStart / 100.0f;

  for (uint8_t i = 0; i < solver_.count() && state_.trailCount < THERMAL_CORE_MAX_TRAIL; i++) {
    // Walk newest first so that if the buffer ever outgrows the trail it is the oldest points
    // that are dropped.
    const uint8_t index = solver_.count() - 1 - i;
    thermal::Observation observation;
    if (!solver_.observationAt(index, observation)) continue;

    const float age = nowS - observation.timeS;
    if (age < 0 || age > thermal::WINDOW_S) continue;

    Vec2 wasAt;
    if (!solver_.positionAt(observation.timeS - thermal::CLIMB_LAG_S, wasAt)) continue;
    const float parcelAge = age + thermal::CLIMB_LAG_S;
    const Vec2 drift = windValid_ ? windToMps_ * parcelAge : Vec2{};
    const Vec2 q = wasAt + drift - groundPosM;

    ThermalCoreTrailPoint& point = state_.trail[state_.trailCount++];
    toBodyFrame(q, headingRad, point.rightM, point.aheadM);

    if (relative) {
      const float z = (observation.climbMps - mean) / span;
      point.lift = z < -0.85f   ? ThermalCoreLift::Sink
                   : z < -0.35f ? ThermalCoreLift::Weak
                   : z < 0.35f  ? ThermalCoreLift::Even
                   : z < 0.85f  ? ThermalCoreLift::Strong
                                : ThermalCoreLift::Best;
    } else {
      const float w = observation.climbMps;
      point.lift = w < 0.0f                     ? ThermalCoreLift::Sink
                   : w < climbStartMps          ? ThermalCoreLift::Weak
                   : w < climbStartMps + 1.0f   ? ThermalCoreLift::Even
                   : w < climbStartMps + 2.5f   ? ThermalCoreLift::Strong
                                                : ThermalCoreLift::Best;
    }
  }

  if (state_.circling && solution.circle.valid) {
    toBodyFrame(solution.circle.centerM, headingRad, state_.centerRightM, state_.centerAheadM);
  } else {
    state_.centerRightM = 0;
    state_.centerAheadM = 0;
  }

  if (state_.coreValid) {
    const Vec2 relativeCore = smoothedCoreM_ - groundPosM;
    toBodyFrame(relativeCore, headingRad, state_.coreRightM, state_.coreAheadM);
    state_.coreDistanceM = thermal::lengthOf(relativeCore);
    state_.centred = state_.coreOffsetM <= CENTRED_M;

    // Flying straight walks the centre of the circle along the nose, which is what re-centring
    // physically is.  So the moment to widen the turn is the one where the nose lines up with
    // the direction from the circle centre to the core -- NOT where it points at the core
    // itself, which never happens: the core is inside the circle, so it stays off to the inside
    // of the nose all the way round.
    const float toCoreRight = state_.coreRightM - state_.centerRightM;
    const float toCoreAhead = state_.coreAheadM - state_.centerAheadM;
    const float openBearing = atan2f(toCoreRight, toCoreAhead) * RAD_TO_DEG_F;
    state_.openNow = !state_.centred && fabsf(openBearing) <= OPEN_CONE_DEG;
  } else {
    state_.coreRightM = 0;
    state_.coreAheadM = 0;
    state_.coreDistanceM = 0;
  }
}

void ThermalCore::updateAverageClimb(float nowS) {
  float sum = 0;
  float weight = 0;
  for (uint8_t i = 0; i < solver_.count(); i++) {
    thermal::Observation observation;
    if (!solver_.observationAt(i, observation)) continue;
    const float age = nowS - observation.timeS;
    if (age < 0 || age > AVERAGE_WINDOW_S) continue;
    sum += observation.climbMps;
    weight += 1.0f;
  }
  state_.avgClimbValid = weight >= 5.0f;
  state_.avgClimbCms =
      state_.avgClimbValid ? static_cast<int16_t>(roundf(sum / weight * 100.0f)) : 0;
}

void ThermalCore::update() {
  float nowS = 0;
  Vec2 groundPosM;
  float headingRad = 0;
  if (!sampleSensors(nowS, groundPosM, headingRad)) return;

  const float dtS = hasLastUpdate_ ? clampF(nowS - lastUpdateS_, 0.0f, 5.0f) : 0.5f;
  lastUpdateS_ = nowS;
  hasLastUpdate_ = true;

  const thermal::CoreSolution solution = solver_.solve(nowS, groundPosM);
  updateCircling(solution.circle, dtS);
  updateCore(solution, nowS, groundPosM, dtS);
  buildTrail(nowS, groundPosM, headingRad, solution);
  updateAverageClimb(nowS);

#ifdef DEBUG_THERMAL_CORE
  // Every number the estimator arrived at, twice a second, for tuning against a recording:
  //   make -C sim OPT="-O1 -g -DDEBUG_THERMAL_CORE"
  // then play sim/recordings/thermal-core.json and read the serial console.  The offsets it
  // prints can be checked against the geometry that recording documents in its own header.
  static uint32_t lastDebug = 0;
  if (millis() - lastDebug >= 2000) {
    lastDebug = millis();
    Serial.printf(
        "TC t=%.0f circ=%d dir=%d R=%.1f used=%d sweep=%.0f rms=%.1f mean=%.2f amp=%.2f "
        "asym=%.2f off=%.1f conf=%d valid=%d open=%d\n",
        nowS, state_.circling, state_.turnDir, solution.circle.radiusM, solution.circle.usedCount,
        solution.circle.sweepDeg, solution.circle.rmsErrM, solution.meanClimbMps,
        solution.amplitudeMps, solution.asymmetry, solution.offsetM, solution.confidence,
        state_.coreValid, state_.openNow);
  }
#endif
}
