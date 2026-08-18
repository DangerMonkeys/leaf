#pragma once

// Pure maths for live thermal centring: where is the core of the thermal we are circling in?
//
// This header deliberately has NO Arduino, FreeRTOS or hardware dependencies so it can be
// compiled and exercised on its own (see tests/thermal_core_tests.cpp).  Everything that reads
// sensors, holds state across ticks or draws pixels lives in thermal_core / page_thermal_core.
//
// Conventions, matching navigation/approach_solver.h:
//   * Positions are metres in a local east/north frame.
//   * Angles are radians; bearings are "east of North" (clockwise from north) where named so.
//   * Vertical speeds are m/s, positive UP.
//
// The central idea is that a thermal is stationary *in the air mass*, not over the ground.  Every
// observation is therefore carried forward to where its parcel of air has drifted to by now
// before anything is fitted to it.  Without that, a 2 m/s wind moves the air 36 m during an 18 s
// lap -- further than the circle radius itself -- and the fit is chasing a target that is not
// there.

#include <math.h>
#include <stdint.h>

namespace thermal {

  // ============================ basic vector maths ============================

  struct Vec2 {
    float x = 0;  // east, metres
    float y = 0;  // north, metres
  };

  inline Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2{a.x + b.x, a.y + b.y}; }
  inline Vec2 operator-(const Vec2& a, const Vec2& b) { return Vec2{a.x - b.x, a.y - b.y}; }
  inline Vec2 operator*(const Vec2& a, float s) { return Vec2{a.x * s, a.y * s}; }
  inline float lengthOf(const Vec2& a) { return sqrtf(a.x * a.x + a.y * a.y); }

  // Shortest signed difference between two angles, in radians, wrapped to (-pi, pi].
  float wrapPi(float radians);

  // ============================ tuning constants ============================

  // How far back the fit looks.  Two and a half laps of a paraglider circle, which is enough to
  // see the shape of the lift twice over without dragging a correction the pilot already flew
  // into the answer for another minute.
  constexpr float WINDOW_S = 45.0f;

  // Recency weighting.  The oldest sample in the window still counts for about a tenth of the
  // newest, so a lap that has just been flown dominates without the previous one being ignored.
  constexpr float WEIGHT_TAU_S = 20.0f;

  // Climb reaching us now describes the air the glider was in this long ago: the barometer's
  // Kalman fusion plus the 1 s average that feeds these observations.  At 9.5 m/s that is 19 m
  // of position error -- most of a circle radius -- if it is not taken out.
  constexpr float CLIMB_LAG_S = 2.0f;

  // Turn geometry the fit is willing to believe.  Below the minimum it is not a thermalling
  // circle, above the maximum it is a fitted line with a huge radius.
  constexpr float MIN_TURN_RADIUS_M = 8.0f;
  constexpr float MAX_TURN_RADIUS_M = 120.0f;

  // The window is trimmed back to the stretch the pilot has actually been circling for.  A
  // thermal is usually entered off a glide, and 10 s of straight run left in the buffer is a
  // 120 m chord that no circle fit survives -- the fitted radius doubles and the lift profile
  // gets read at the wrong bearings.  Walking back until the turning stops is a cleaner cut
  // than any fixed time.
  constexpr float SEGMENT_PROBE_S = 4.0f;
  constexpr float SEGMENT_MIN_TURN_DEG = 25.0f;  // over one probe, below which it is a glide
  constexpr float SEGMENT_MAX_SWEEP_DEG = 720.0f;  // two laps back is as much history as helps

  // Converting the lap's lift asymmetry into a distance.
  //
  // On one circle of constant radius the lift profile pins down only the *shape* parameter of a
  // Gaussian core (how lopsided the lap is), never the core's width and offset separately -- the
  // two are exactly degenerate.  So rather than assume a core width, the offset is expressed as a
  // fraction of the circle being flown, which is also how pilots think about it ("shift half a
  // radius towards the strong side").
  //
  // GAIN is set from the reference flight in sim/recordings/thermal-core.json flown through the
  // real firmware: a 27 m circle one radius off the core measures an asymmetry of 0.50 once the
  // climb has been through the barometer's fusion, the 1 s average and a 1 Hz harmonic fit, so
  // 2.0 would read that offset back exactly.  It is deliberately set below that.  An offset that
  // under-reads walks the pilot in from one side over two laps; one that over-reads flies them
  // past the core and hunts, and the same constant has to serve cores narrower and wider than
  // this one, whose width a single circle cannot measure.
  constexpr float ASYMMETRY_GAIN = 1.7f;
  constexpr float MAX_OFFSET_RATIO = 1.25f;  // never claim the core is further than this * radius

  // Mean climb below this makes the asymmetry ratio meaningless, so it floors the denominator.
  constexpr float MIN_MEAN_CLIMB_MPS = 0.3f;

  // ============================ observations ============================

  constexpr uint8_t MAX_OBSERVATIONS = 64;

  // One look at the air: where the glider was, and what the vario read there.
  struct Observation {
    float timeS = 0;
    Vec2 posM;  // ground frame
    float climbMps = 0;
  };

  // ============================ results ============================

  struct CircleFit {
    bool valid = false;
    Vec2 centerM;          // air frame, relative to the glider's position now
    float radiusM = 0;
    float rmsErrM = 0;     // how circular the flown path actually was
    float sweepDeg = 0;    // signed turn covered by the segment: + clockwise (right), - left
    float turnRateDegS = 0;  // signed, over the last few seconds only
    float segmentStartS = 0;  // oldest time the fit was willing to use
    uint8_t usedCount = 0;
  };

  struct CoreSolution {
    bool valid = false;
    CircleFit circle;

    Vec2 coreM;              // air frame, relative to the glider's position now
    float offsetM = 0;       // circle centre to core
    float meanClimbMps = 0;  // lap mean, which is what the thermal is actually worth
    float amplitudeMps = 0;  // half the peak-to-peak swing around the lap
    float asymmetry = 0;     // amplitude / mean, clamped to [0, 1]; 0 is a centred lap
    uint8_t confidence = 0;  // 0..100
  };

  // ============================ the solver ============================

  // Owns the ring of observations and turns it into a core estimate.  Stateless between solves:
  // every call re-derives the answer from the ring, which keeps it trivially testable.
  class CoreSolver {
   public:
    void reset();

    // Direction the wind blows TOWARD, m/s.  Ignored while invalid, which costs accuracy in wind
    // but never produces a worse answer than the ground frame would have given.
    void setWind(Vec2 windToMps, bool valid);

    // Observations must arrive in time order.  Duplicated or out-of-order times are dropped.
    void addObservation(float timeS, Vec2 posM, float climbMps);

    uint8_t count() const { return count_; }

    // Observations oldest-first, for callers that need to draw them as well as fit them.
    bool observationAt(uint8_t indexFromOldest, Observation& out) const;
    float newestTimeS() const;
    float oldestTimeS() const;

    // Ground position the glider occupied at an arbitrary time, linearly interpolated between
    // observations.  Returns false outside the buffer's span.
    bool positionAt(float timeS, Vec2& out) const;

    CircleFit fitCircle(float nowS, Vec2 nowPosM) const;
    CoreSolution solve(float nowS, Vec2 nowPosM) const;

   private:
    const Observation& at(uint8_t indexFromOldest) const;

    Observation ring_[MAX_OBSERVATIONS];
    uint8_t next_ = 0;
    uint8_t count_ = 0;
    Vec2 windToMps_;
    bool windValid_ = false;
  };

  // Solves a 3x3 symmetric system by Cramer's rule.  Returns false if it is too near singular to
  // trust, which is what an under-determined fit looks like from the inside.
  bool solve3x3(const float a[3][3], const float b[3], float out[3]);

}  // namespace thermal
