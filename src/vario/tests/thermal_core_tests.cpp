#include "tests/thermal_core_tests.h"

#include <Arduino.h>
#include <math.h>

#include "navigation/thermal_core_solver.h"

namespace {
  using namespace thermal;

  uint16_t failures = 0;

  void expect(bool condition, const char* what) {
    if (condition) return;
    failures++;
    Serial.print("  FAIL: ");
    Serial.println(what);
  }

  void expectNear(float actual, float expected, float tolerance, const char* what) {
    if (fabsf(actual - expected) <= tolerance) return;
    failures++;
    Serial.print("  FAIL: ");
    Serial.print(what);
    Serial.print(" (got ");
    Serial.print(actual, 3);
    Serial.print(", expected ");
    Serial.print(expected, 3);
    Serial.println(")");
  }

  // ---------------------------------------------------------------- flight generator
  //
  // A glider circling at a steady rate in an air mass that is itself moving over the ground,
  // with a Gaussian thermal core sitting still *in that air mass*.  This is the situation the
  // solver exists for, and everything below is measured against the geometry that produced it.

  struct Flight {
    Vec2 windToMps;        // the air mass over the ground
    Vec2 circleCenter;     // air-frame centre of the glider's circle
    float radiusM = 27.0f;
    float turnRateDegS = 20.0f;  // positive is a right-hand (clockwise) turn
    float startBearingDeg = 0;

    // The reference core from sim/recordings/thermal-core.json, recovered from the climb rates
    // that recording states: a 27 m circle one radius off it reads -0.2 to +4.2 m/s, and the same
    // circle centred reads +2.26.  Only those two facts fix the strength and width of a Gaussian.
    Vec2 core;                 // air-frame position of the core
    float coreStrengthMps = 5.2f;
    float coreRadiusM = 39.5f;  // e-folding radius of the Gaussian
    float gliderSinkMps = 1.0f;
    float climbLagS = CLIMB_LAG_S;  // how late the vario reports what it flew through

    // Where the glider sits in the air mass at time t.
    Vec2 airPosAt(float t) const {
      const float bearing = (startBearingDeg + turnRateDegS * t) * 0.0174532925f;
      return circleCenter + Vec2{sinf(bearing) * radiusM, cosf(bearing) * radiusM};
    }

    // Where it sits over the ground: the air it is in has drifted downwind since t = 0.
    Vec2 groundPosAt(float t) const { return airPosAt(t) + windToMps * t; }

    // What the vario reads at time t, which describes the air of climbLagS ago.
    float climbAt(float t) const {
      const Vec2 was = airPosAt(t - climbLagS);
      const Vec2 d = was - core;
      const float r2 = d.x * d.x + d.y * d.y;
      return coreStrengthMps * expf(-r2 / (coreRadiusM * coreRadiusM)) - gliderSinkMps;
    }
  };

  // Fly `seconds` of it into a solver at 1 Hz, which is the rate the GPS supplies.
  void fly(CoreSolver& solver, const Flight& f, float seconds, bool tellItTheWind = true) {
    solver.setWind(f.windToMps, tellItTheWind);
    for (float t = 0; t <= seconds + 1e-3f; t += 1.0f) {
      solver.addObservation(t, f.groundPosAt(t), f.climbAt(t));
    }
  }

  // ---------------------------------------------------------------- the linear solver

  void testSolve3x3() {
    const float a[3][3] = {{2, 1, 0}, {1, 3, 1}, {0, 1, 4}};
    const float b[3] = {5, 10, 14};
    float x[3];
    expect(solve3x3(a, b, x), "well-conditioned system solves");
    expectNear(x[0], 1.6111f, 0.001f, "solve3x3 x");
    expectNear(x[1], 1.7778f, 0.001f, "solve3x3 y");
    expectNear(x[2], 3.0556f, 0.001f, "solve3x3 z");

    // Two identical rows carry no information about a third unknown.
    const float singular[3][3] = {{1, 2, 3}, {1, 2, 3}, {2, 4, 6}};
    float ignored[3];
    expect(!solve3x3(singular, b, ignored), "singular system is refused");
  }

  // ---------------------------------------------------------------- turn geometry

  void testCircleFitStillAir() {
    Flight f;
    f.circleCenter = Vec2{100, 200};
    f.core = f.circleCenter;

    CoreSolver solver;
    fly(solver, f, 40.0f);
    const Vec2 now = f.groundPosAt(40.0f);
    const CircleFit fit = solver.fitCircle(40.0f, now);

    expect(fit.valid, "a flown circle fits");
    expectNear(fit.radiusM, f.radiusM, 1.5f, "fitted radius");

    // The fit is relative to the glider, so the expected centre is too.
    const Vec2 expected = f.circleCenter - f.airPosAt(40.0f);
    expectNear(fit.centerM.x, expected.x, 2.0f, "fitted centre east");
    expectNear(fit.centerM.y, expected.y, 2.0f, "fitted centre north");

    // The window holds 40 s of a 20 deg/s turn, which is 780 degrees, but the fit stops walking
    // back at two laps: older than that is a circle the pilot has already moved on from.
    expectNear(fit.sweepDeg, SEGMENT_MAX_SWEEP_DEG, 25.0f, "sweep is capped at two laps");
    expectNear(fit.turnRateDegS, 20.0f, 3.0f, "turn rate");
  }

  void testTurnDirectionSign() {
    Flight right;
    right.circleCenter = Vec2{0, 0};
    right.core = Vec2{0, 0};
    CoreSolver rightSolver;
    fly(rightSolver, right, 40.0f);
    expect(rightSolver.fitCircle(40.0f, right.groundPosAt(40.0f)).sweepDeg > 300.0f,
           "a right-hand turn sweeps positive");

    Flight left = right;
    left.turnRateDegS = -20.0f;
    CoreSolver leftSolver;
    fly(leftSolver, left, 40.0f);
    expect(leftSolver.fitCircle(40.0f, left.groundPosAt(40.0f)).sweepDeg < -300.0f,
           "a left-hand turn sweeps negative");
  }

  // The single most important thing the solver does.  A 2 m/s wind moves the air 36 m during an
  // 18 s lap, further than the 27 m circle radius, so over the ground the path is a stretched
  // spiral and not a circle at all.
  void testWindMakesTheSpiralACircleAgain() {
    Flight f;
    f.windToMps = Vec2{2.0f, 0.5f};
    f.circleCenter = Vec2{0, 0};
    f.core = Vec2{0, 0};

    CoreSolver told;
    fly(told, f, 40.0f, true);
    const CircleFit good = told.fitCircle(40.0f, f.groundPosAt(40.0f));
    expect(good.valid, "wind-compensated path fits a circle");
    expectNear(good.radiusM, f.radiusM, 2.0f, "wind-compensated radius");
    expect(good.rmsErrM < 2.0f, "wind-compensated path is round");

    CoreSolver blind;
    fly(blind, f, 40.0f, false);
    const CircleFit bad = blind.fitCircle(40.0f, f.groundPosAt(40.0f));
    expect(bad.rmsErrM > 3.0f * good.rmsErrM,
           "the same path over the ground is measurably not a circle");
  }

  // A thermal is entered off a glide, and the straight run in is still sitting in the buffer for
  // the first half minute of circling.  It is a 120 m chord that no circle fit survives, so the
  // fit has to find where the turning started and cut there.
  void testGlideBeforeTheTurnIsCutOut() {
    Flight f;
    f.circleCenter = Vec2{0, 0};
    f.core = f.circleCenter;

    CoreSolver solver;
    solver.setWind(Vec2{}, true);
    // 12 s of straight glide arriving at the point the circle starts from.
    const Vec2 entry = f.airPosAt(0);
    for (float t = -12.0f; t < 0.0f; t += 1.0f) {
      solver.addObservation(t + 20.0f, entry + Vec2{11.0f * t, 0}, -1.2f);
    }
    for (float t = 0; t <= 30.0f + 1e-3f; t += 1.0f) {
      solver.addObservation(t + 20.0f, f.groundPosAt(t), f.climbAt(t));
    }

    const CircleFit fit = solver.fitCircle(50.0f, f.groundPosAt(30.0f));
    expect(fit.valid, "the circle after a glide still fits");
    expectNear(fit.radiusM, f.radiusM, 3.0f, "the glide does not inflate the fitted radius");
    expect(fit.segmentStartS > 18.0f, "the fit starts at the turn, not at the glide");
    expect(fit.rmsErrM < 3.0f, "the fitted segment is round");
  }

  // ---------------------------------------------------------------- the core estimate

  // Bearing from the circle centre to wherever the solver thinks the core is.
  float estimatedCoreBearingDeg(const CoreSolution& s) {
    const Vec2 v = s.coreM - s.circle.centerM;
    return atan2f(v.x, v.y) * 57.2957795f;
  }

  float trueCoreBearingDeg(const Flight& f) {
    const Vec2 v = f.core - f.circleCenter;
    return atan2f(v.x, v.y) * 57.2957795f;
  }

  void testCoreDirectionInStillAir() {
    // A circle exactly one radius off the core, in four different directions.
    const float bearings[4] = {0.0f, 90.0f, 180.0f, -90.0f};
    for (uint8_t i = 0; i < 4; i++) {
      const float rad = bearings[i] * 0.0174532925f;
      Flight f;
      f.circleCenter = Vec2{0, 0};
      f.core = Vec2{sinf(rad) * f.radiusM, cosf(rad) * f.radiusM};

      CoreSolver solver;
      fly(solver, f, 45.0f);
      const CoreSolution s = solver.solve(45.0f, f.groundPosAt(45.0f));

      expect(s.valid, "an offset circle produces a core estimate");
      const float err = wrapPi((estimatedCoreBearingDeg(s) - bearings[i]) * 0.0174532925f) *
                        57.2957795f;
      expectNear(err, 0.0f, 15.0f, "core direction in still air");
      expect(s.offsetM > 0.5f * f.radiusM, "an offset circle reads as offset");
      expect(s.confidence > 50, "a clean offset lap is believed");
    }
  }

  void testCoreDirectionInWind() {
    Flight f;
    f.windToMps = Vec2{2.0f, 0.5f};
    f.circleCenter = Vec2{0, 0};
    f.core = Vec2{f.radiusM, 0};  // one radius due east of the circle centre

    CoreSolver solver;
    fly(solver, f, 45.0f);
    const CoreSolution s = solver.solve(45.0f, f.groundPosAt(45.0f));
    expect(s.valid, "a core estimate survives wind");
    const float err =
        wrapPi((estimatedCoreBearingDeg(s) - trueCoreBearingDeg(f)) * 0.0174532925f) * 57.2957795f;
    expectNear(err, 0.0f, 20.0f, "core direction in wind");
  }

  // A centred circle has nothing to say, and must say so rather than inventing a direction.
  void testCentredCircleReadsCentred() {
    Flight f;
    f.circleCenter = Vec2{0, 0};
    f.core = Vec2{1.0f, 0};  // a metre out, which is as centred as anyone flies

    CoreSolver solver;
    fly(solver, f, 45.0f);
    const CoreSolution s = solver.solve(45.0f, f.groundPosAt(45.0f));
    expect(s.valid, "a centred circle still solves");
    expect(s.asymmetry < 0.15f, "a centred lap reads symmetric");
    expect(s.offsetM < 6.0f, "a centred lap puts the core near the circle centre");
    expect(s.meanClimbMps > 2.0f, "a centred lap is climbing well");
  }

  // Climb arrives about two seconds after the air that caused it.  Left uncorrected that rotates
  // the estimate by two seconds of turn -- 40 degrees at 20 deg/s -- which is the difference
  // between a useful marker and one that sends the pilot the wrong way round the circle.
  void testLagCompensation() {
    Flight f;
    f.circleCenter = Vec2{0, 0};
    f.core = Vec2{0, f.radiusM};  // due north of the circle centre

    CoreSolver solver;
    fly(solver, f, 45.0f);
    const CoreSolution s = solver.solve(45.0f, f.groundPosAt(45.0f));
    const float err =
        wrapPi((estimatedCoreBearingDeg(s) - trueCoreBearingDeg(f)) * 0.0174532925f) * 57.2957795f;
    expectNear(err, 0.0f, 15.0f, "core direction is not rotated by vario lag");

    // The size of the error the compensation removes: a flight whose vario lags twice as much
    // should be visibly worse, which proves the correction is doing the work and not the fit.
    Flight laggy = f;
    laggy.climbLagS = CLIMB_LAG_S * 3.0f;
    CoreSolver laggySolver;
    fly(laggySolver, laggy, 45.0f);
    const CoreSolution ls = laggySolver.solve(45.0f, laggy.groundPosAt(45.0f));
    const float laggyErr =
        wrapPi((estimatedCoreBearingDeg(ls) - trueCoreBearingDeg(laggy)) * 0.0174532925f) *
        57.2957795f;
    expect(fabsf(laggyErr) > fabsf(err) + 15.0f, "unmodelled extra lag rotates the estimate");
  }

  void testOffsetGrowsWithDistance() {
    float previous = -1;
    for (uint8_t step = 0; step < 4; step++) {
      Flight f;
      f.circleCenter = Vec2{0, 0};
      f.core = Vec2{0, 9.0f * step};

      CoreSolver solver;
      fly(solver, f, 45.0f);
      const CoreSolution s = solver.solve(45.0f, f.groundPosAt(45.0f));
      expect(s.offsetM > previous, "a further core reads as further out");
      expect(s.offsetM <= f.radiusM * MAX_OFFSET_RATIO + 0.01f, "offset stays inside its bound");
      previous = s.offsetM;
    }
  }

  // ---------------------------------------------------------------- refusing to answer

  void testHalfALapIsNotEnough() {
    Flight f;
    f.circleCenter = Vec2{0, 0};
    f.core = Vec2{f.radiusM, 0};

    CoreSolver solver;
    fly(solver, f, 9.0f);  // 180 degrees
    const CoreSolution s = solver.solve(9.0f, f.groundPosAt(9.0f));
    expect(s.confidence == 0, "half a lap is not believed");
  }

  void testStraightFlightHasNoCircle() {
    CoreSolver solver;
    solver.setWind(Vec2{}, true);
    for (float t = 0; t <= 40.0f; t += 1.0f) {
      solver.addObservation(t, Vec2{11.0f * t, 0}, -1.2f);
    }
    const CoreSolution s = solver.solve(40.0f, Vec2{440.0f, 0});
    expect(!s.valid || s.confidence == 0, "a straight glide produces no core");
    expect(fabsf(s.circle.turnRateDegS) < 3.0f, "a straight glide reports no turn rate");
  }

  void testSinkIsNotAThermal() {
    Flight f;
    f.circleCenter = Vec2{0, 0};
    f.core = Vec2{f.radiusM, 0};
    f.coreStrengthMps = 0.4f;  // a dying bubble, never worth a marker

    CoreSolver solver;
    fly(solver, f, 45.0f);
    const CoreSolution s = solver.solve(45.0f, f.groundPosAt(45.0f));
    expect(s.confidence < 25, "circling in sink is not believed");
  }

  // ---------------------------------------------------------------- the ring buffer

  void testRingBufferOrdering() {
    CoreSolver solver;
    for (uint16_t i = 0; i < MAX_OBSERVATIONS + 20; i++) {
      solver.addObservation(static_cast<float>(i), Vec2{static_cast<float>(i), 0}, 1.0f);
    }
    expect(solver.count() == MAX_OBSERVATIONS, "the ring fills and stops growing");
    expectNear(solver.newestTimeS(), MAX_OBSERVATIONS + 19.0f, 0.001f, "newest sample is kept");
    expectNear(solver.oldestTimeS(), 20.0f, 0.001f, "oldest sample is dropped");

    // Out-of-order and duplicate fixes happen when the receiver stumbles; they must not corrupt
    // the interpolation the lag correction depends on.
    const float newest = solver.newestTimeS();
    solver.addObservation(newest - 5.0f, Vec2{0, 0}, 9.0f);
    solver.addObservation(newest, Vec2{0, 0}, 9.0f);
    expectNear(solver.newestTimeS(), newest, 0.001f, "stale fixes are dropped");

    Vec2 p;
    expect(solver.positionAt(25.5f, p), "interpolation inside the buffer");
    expectNear(p.x, 25.5f, 0.001f, "interpolated position");
    expect(!solver.positionAt(5.0f, p), "interpolation before the buffer is refused");
    expect(!solver.positionAt(newest + 5.0f, p), "interpolation after the buffer is refused");
  }

}  // namespace

void test_thermal_core_solver(void) {
  Serial.println("Thermal core solver tests:");
  failures = 0;

  testSolve3x3();
  testCircleFitStillAir();
  testTurnDirectionSign();
  testWindMakesTheSpiralACircleAgain();
  testGlideBeforeTheTurnIsCutOut();
  testCoreDirectionInStillAir();
  testCoreDirectionInWind();
  testCentredCircleReadsCentred();
  testLagCompensation();
  testOffsetGrowsWithDistance();
  testHalfALapIsNotEnough();
  testStraightFlightHasNoCircle();
  testSinkIsNotAThermal();
  testRingBufferOrdering();

  if (failures == 0) {
    Serial.println("  thermal core solver: all tests passed.");
  } else {
    Serial.print("  thermal core solver: ");
    Serial.print(failures);
    Serial.println(" failure(s).");
  }
}
