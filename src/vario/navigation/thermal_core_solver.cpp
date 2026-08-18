#include "navigation/thermal_core_solver.h"

namespace thermal {

  namespace {
    constexpr float PI_F = 3.14159265f;
    constexpr float TWO_PI_F = 6.28318531f;
    constexpr float RAD_TO_DEG_F = 57.2957795f;

    // Position deltas shorter than this are noise, not a heading.
    constexpr float MIN_HEADING_DELTA_M = 0.8f;

    // The turn rate the circling state machine reacts to is taken over this much of the recent
    // path: long enough to ride out one ragged GPS fix, short enough that rolling out of a turn
    // shows up within a couple of seconds.
    constexpr float TURN_RATE_SPAN_S = 4.0f;

    // Bearing "east of North": grows clockwise, so a right-hand turn accumulates positive sweep.
    inline float bearingOf(const Vec2& v) { return atan2f(v.x, v.y); }

    inline Vec2 fromBearing(float bearingRad, float lengthM) {
      return Vec2{sinf(bearingRad) * lengthM, cosf(bearingRad) * lengthM};
    }

    inline float clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
  }  // namespace

  float wrapPi(float radians) {
    while (radians > PI_F) radians -= TWO_PI_F;
    while (radians <= -PI_F) radians += TWO_PI_F;
    return radians;
  }

  bool solve3x3(const float a[3][3], const float b[3], float out[3]) {
    const float c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    const float c01 = a[1][0] * a[2][2] - a[1][2] * a[2][0];
    const float c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    const float det = a[0][0] * c00 - a[0][1] * c01 + a[0][2] * c02;

    // Scale the singularity test to the size of the matrix so it means the same thing whether the
    // fit is over metres or over unit vectors.  An under-determined fit looks exactly like this
    // from the inside, which is why it is a return value and not an assert.
    float scale = 0;
    for (uint8_t r = 0; r < 3; r++) {
      for (uint8_t c = 0; c < 3; c++) {
        const float v = fabsf(a[r][c]);
        if (v > scale) scale = v;
      }
    }
    if (scale <= 0 || fabsf(det) < 1e-6f * scale * scale * scale) return false;

    const float inv = 1.0f / det;
    const float d1 = b[1] * a[2][2] - a[1][2] * b[2];
    const float d2 = b[1] * a[2][1] - a[1][1] * b[2];
    const float d3 = a[1][0] * b[2] - b[1] * a[2][0];
    const float d4 = a[1][1] * b[2] - b[1] * a[2][1];

    out[0] = inv * (b[0] * c00 - a[0][1] * d1 + a[0][2] * d2);
    out[1] = inv * (a[0][0] * d1 - b[0] * c01 + a[0][2] * d3);
    out[2] = inv * (a[0][0] * d4 - a[0][1] * d3 + b[0] * c02);
    return true;
  }

  // ============================ ring management ============================

  void CoreSolver::reset() {
    next_ = 0;
    count_ = 0;
    windValid_ = false;
    windToMps_ = Vec2{};
  }

  void CoreSolver::setWind(Vec2 windToMps, bool valid) {
    windToMps_ = windToMps;
    windValid_ = valid;
  }

  const Observation& CoreSolver::at(uint8_t indexFromOldest) const {
    const uint8_t oldest = (next_ + MAX_OBSERVATIONS - count_) % MAX_OBSERVATIONS;
    return ring_[(oldest + indexFromOldest) % MAX_OBSERVATIONS];
  }

  float CoreSolver::newestTimeS() const { return count_ == 0 ? 0 : at(count_ - 1).timeS; }
  float CoreSolver::oldestTimeS() const { return count_ == 0 ? 0 : at(0).timeS; }

  void CoreSolver::addObservation(float timeS, Vec2 posM, float climbMps) {
    if (count_ > 0 && timeS <= newestTimeS()) return;
    ring_[next_] = Observation{timeS, posM, climbMps};
    next_ = (next_ + 1) % MAX_OBSERVATIONS;
    if (count_ < MAX_OBSERVATIONS) count_++;
  }

  bool CoreSolver::observationAt(uint8_t indexFromOldest, Observation& out) const {
    if (indexFromOldest >= count_) return false;
    out = at(indexFromOldest);
    return true;
  }

  bool CoreSolver::positionAt(float timeS, Vec2& out) const {
    if (count_ == 0) return false;
    if (timeS < at(0).timeS || timeS > at(count_ - 1).timeS) return false;
    for (uint8_t i = 1; i < count_; i++) {
      const Observation& b = at(i);
      if (b.timeS < timeS) continue;
      const Observation& a = at(i - 1);
      const float span = b.timeS - a.timeS;
      const float f = span > 1e-3f ? (timeS - a.timeS) / span : 0.0f;
      out = a.posM + (b.posM - a.posM) * f;
      return true;
    }
    out = at(count_ - 1).posM;
    return true;
  }

  // ============================ turn geometry ============================

  CircleFit CoreSolver::fitCircle(float nowS, Vec2 nowPosM) const {
    CircleFit fit;
    if (count_ < 6) return fit;

    // Carry each observation forward to where its parcel of air has drifted to by now, and
    // express it relative to the glider.  In still air this is just the ground track; in wind it
    // is the shape the pilot flew through the air, which is the only shape a circle fit can
    // succeed on.
    Vec2 q[MAX_OBSERVATIONS];
    float weight[MAX_OBSERVATIONS];
    float times[MAX_OBSERVATIONS];
    uint8_t n = 0;
    for (uint8_t i = 0; i < count_; i++) {
      const Observation& o = at(i);
      const float age = nowS - o.timeS;
      if (age < 0 || age > WINDOW_S) continue;
      const Vec2 drift = windValid_ ? windToMps_ * age : Vec2{};
      q[n] = o.posM + drift - nowPosM;
      weight[n] = expf(-age / WEIGHT_TAU_S);
      times[n] = o.timeS;
      n++;
    }
    if (n < 6) return fit;

    // Turn per step, taken from the path itself rather than from GPS course, which keeps it
    // usable before there is any circle to measure against.
    float turn[MAX_OBSERVATIONS] = {};
    float priorBearing = 0;
    bool hasPrior = false;
    for (uint8_t i = 1; i < n; i++) {
      const Vec2 step = q[i] - q[i - 1];
      if (lengthOf(step) < MIN_HEADING_DELTA_M) continue;
      const float bearing = bearingOf(step);
      if (hasPrior) turn[i] = wrapPi(bearing - priorBearing);
      priorBearing = bearing;
      hasPrior = true;
    }

    float recentSweepRad = 0;
    float recentStartS = nowS;
    for (uint8_t i = 1; i < n; i++) {
      if (times[i] < nowS - TURN_RATE_SPAN_S) continue;
      recentSweepRad += turn[i];
      if (times[i - 1] < recentStartS) recentStartS = times[i - 1];
    }
    const float recentSpanS = nowS - recentStartS;
    if (recentSpanS > 0.5f) fit.turnRateDegS = recentSweepRad * RAD_TO_DEG_F / recentSpanS;

    // Walk back from the newest sample and stop where the turning stopped: everything older
    // belongs to a glide or to a previous, differently placed circle, and fitting through it is
    // what makes the radius wrong just as the pilot most needs it.
    float cumulative[MAX_OBSERVATIONS] = {};
    for (uint8_t i = 1; i < n; i++) cumulative[i] = cumulative[i - 1] + fabsf(turn[i]);

    const float maxSweepRad = SEGMENT_MAX_SWEEP_DEG / RAD_TO_DEG_F;
    const float minProbeRad = SEGMENT_MIN_TURN_DEG / RAD_TO_DEG_F;
    uint8_t start = 0;
    for (int16_t i = static_cast<int16_t>(n) - 2; i >= 1; i--) {
      if (cumulative[n - 1] - cumulative[i] > maxSweepRad) {
        start = static_cast<uint8_t>(i);
        break;
      }
      int16_t k = i;
      while (k > 0 && times[i] - times[k - 1] <= SEGMENT_PROBE_S) k--;
      const float probeSpanS = times[i] - times[k];
      if (probeSpanS >= 0.75f * SEGMENT_PROBE_S && (cumulative[i] - cumulative[k]) < minProbeRad) {
        start = static_cast<uint8_t>(i);
        break;
      }
    }

    float sweepRad = 0;
    for (uint8_t i = start + 1; i < n; i++) sweepRad += turn[i];
    fit.sweepDeg = sweepRad * RAD_TO_DEG_F;
    fit.segmentStartS = times[start];

    const uint8_t used = n - start;
    if (used < 6) return fit;
    fit.usedCount = used;

    // Kasa circle fit: minimise the algebraic residual |p|^2 - (2 cx x + 2 cy y + k), which is a
    // plain weighted least squares in [cx, cy, k] and needs no iteration.
    float a[3][3] = {};
    float b[3] = {};
    for (uint8_t i = start; i < n; i++) {
      const float f[3] = {2.0f * q[i].x, 2.0f * q[i].y, 1.0f};
      const float g = q[i].x * q[i].x + q[i].y * q[i].y;
      for (uint8_t r = 0; r < 3; r++) {
        for (uint8_t c = 0; c < 3; c++) a[r][c] += weight[i] * f[r] * f[c];
        b[r] += weight[i] * f[r] * g;
      }
    }
    float sol[3];
    if (!solve3x3(a, b, sol)) return fit;

    const float r2 = sol[2] + sol[0] * sol[0] + sol[1] * sol[1];
    if (r2 <= 0) return fit;
    fit.centerM = Vec2{sol[0], sol[1]};
    fit.radiusM = sqrtf(r2);
    if (fit.radiusM < MIN_TURN_RADIUS_M || fit.radiusM > MAX_TURN_RADIUS_M) return fit;

    float sumSq = 0;
    float sumW = 0;
    for (uint8_t i = start; i < n; i++) {
      const float e = lengthOf(q[i] - fit.centerM) - fit.radiusM;
      sumSq += weight[i] * e * e;
      sumW += weight[i];
    }
    fit.rmsErrM = sumW > 0 ? sqrtf(sumSq / sumW) : 0;
    fit.valid = true;
    return fit;
  }

  // ============================ the lift field ============================

  CoreSolution CoreSolver::solve(float nowS, Vec2 nowPosM) const {
    CoreSolution out;
    out.circle = fitCircle(nowS, nowPosM);
    if (!out.circle.valid) return out;

    // Pair each climb reading with the air the glider was in when the pressure that produced it
    // started moving, then fit lift(bearing) = a0 + a1 cos + b1 sin around the lap.  The first
    // harmonic is the whole answer: its phase is the direction of the core from the circle
    // centre, and its size relative to the mean is how far out the circle is.
    float a[3][3] = {};
    float b[3] = {};
    float theta[MAX_OBSERVATIONS];
    float climb[MAX_OBSERVATIONS];
    float weight[MAX_OBSERVATIONS];
    uint8_t n = 0;

    for (uint8_t i = 0; i < count_; i++) {
      const Observation& o = at(i);
      const float age = nowS - o.timeS;
      if (age < 0 || age > WINDOW_S) continue;
      if (o.timeS < out.circle.segmentStartS) continue;

      Vec2 wasAt;
      if (!positionAt(o.timeS - CLIMB_LAG_S, wasAt)) continue;
      const float parcelAge = age + CLIMB_LAG_S;
      const Vec2 drift = windValid_ ? windToMps_ * parcelAge : Vec2{};
      const Vec2 q = wasAt + drift - nowPosM;

      const Vec2 fromCenter = q - out.circle.centerM;
      if (lengthOf(fromCenter) < 1e-3f) continue;

      theta[n] = bearingOf(fromCenter);
      climb[n] = o.climbMps;
      weight[n] = expf(-age / WEIGHT_TAU_S);

      const float f[3] = {1.0f, cosf(theta[n]), sinf(theta[n])};
      for (uint8_t r = 0; r < 3; r++) {
        for (uint8_t c = 0; c < 3; c++) a[r][c] += weight[n] * f[r] * f[c];
        b[r] += weight[n] * f[r] * climb[n];
      }
      n++;
    }
    if (n < 6) return out;

    float sol[3];
    if (!solve3x3(a, b, sol)) return out;

    out.meanClimbMps = sol[0];
    out.amplitudeMps = sqrtf(sol[1] * sol[1] + sol[2] * sol[2]);
    const float phase = atan2f(sol[2], sol[1]);

    const float mean =
        out.meanClimbMps > MIN_MEAN_CLIMB_MPS ? out.meanClimbMps : MIN_MEAN_CLIMB_MPS;
    out.asymmetry = clampF(out.amplitudeMps / (2.0f * mean), 0.0f, 1.0f);
    out.offsetM = clampF(out.circle.radiusM * ASYMMETRY_GAIN * out.asymmetry, 0.0f,
                         out.circle.radiusM * MAX_OFFSET_RATIO);
    out.coreM = out.circle.centerM + fromBearing(phase, out.offsetM);

    // How much of this to believe.  Each term is a different way the answer can be wrong, and
    // they multiply because any one of them being bad is enough to make the marker misleading.
    float residSq = 0;
    float sumW = 0;
    for (uint8_t i = 0; i < n; i++) {
      const float model = sol[0] + sol[1] * cosf(theta[i]) + sol[2] * sinf(theta[i]);
      const float e = climb[i] - model;
      residSq += weight[i] * e * e;
      sumW += weight[i];
    }
    const float resid = sumW > 0 ? sqrtf(residSq / sumW) : 0;

    const float coverage = clampF((fabsf(out.circle.sweepDeg) - 240.0f) / 120.0f, 0.0f, 1.0f);
    // Real circling is ragged: a few metres of scatter on a 28 m circle is a pilot flying, not a
    // fit failing, and only a path that is not a circle at all should reach zero here.
    const float roundness =
        clampF(1.0f - out.circle.rmsErrM / (0.45f * out.circle.radiusM), 0.0f, 1.0f);
    const float lift = clampF((out.meanClimbMps - 0.1f) / 0.6f, 0.0f, 1.0f);
    // The signal term asks whether the shape of the lap is real or is noise.  The floor matters:
    // a lap with no asymmetry and no scatter is a *centred* lap, which is the best answer this
    // can give and must not be scored as an absent one -- only scatter without shape is doubt.
    constexpr float SIGNAL_FLOOR_MPS = 0.25f;
    const float signal = (out.amplitudeMps + SIGNAL_FLOOR_MPS) /
                         (out.amplitudeMps + 2.0f * resid + SIGNAL_FLOOR_MPS);

    out.confidence =
        static_cast<uint8_t>(clampF(100.0f * coverage * roundness * lift * signal, 0.0f, 100.0f));
    out.valid = true;
    return out;
  }

}  // namespace thermal
