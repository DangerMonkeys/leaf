#pragma once

#include <Arduino.h>

#include "navigation/thermal_core_solver.h"

// Live thermal centring.  Samples the GPS and the vario while the pilot is circling, hands the
// result to thermal::CoreSolver, and holds the answer in the shape the Thermal Core page draws:
// everything already rotated into the glider's own frame, in metres, so the page only scales.
//
// Two things about the picture are deliberate and worth knowing before reading the page code:
//
//   * The trail is drawn in the AIR MASS, not over the ground.  A centred circle therefore draws
//     as a closed circle rather than the downwind spiral the ground track actually is.
//   * Each trail point sits where the air it describes was, which is about two seconds of flight
//     behind where the glider was when the vario reported it.  The newest thing anyone can know
//     about the air is that old, and pretending otherwise puts the lift a fifth of a lap out.

constexpr uint8_t THERMAL_CORE_MAX_TRAIL = thermal::MAX_OBSERVATIONS;

// Lift at a point of the trail, banded for drawing.  While circling the bands are relative to
// the lap's own mean, so the display shows the *shape* of the thermal rather than saturating at
// full size everywhere inside a strong one.  Gliding, they are absolute.
enum class ThermalCoreLift : uint8_t {
  Sink = 0,
  Weak = 1,
  Even = 2,
  Strong = 3,
  Best = 4,
};

struct ThermalCoreTrailPoint {
  float rightM = 0;  // positive to the right of the nose
  float aheadM = 0;  // positive ahead of the glider
  ThermalCoreLift lift = ThermalCoreLift::Even;
};

struct ThermalCoreState {
  // --- turn state -----------------------------------------------------------------------
  bool circling = false;
  int8_t turnDir = 0;      // +1 circling right, -1 circling left, 0 not circling
  float turnRadiusM = 0;   // 0 unless circling

  // The radius, in metres, the map should cover so the flown circle fills it.  Smoothed, and
  // defined while gliding too, so the view never jumps as the turn starts or stops.
  float mapScaleRadiusM = 0;

  // --- the core -------------------------------------------------------------------------
  bool coreValid = false;
  float coreRightM = 0;
  float coreAheadM = 0;
  float coreDistanceM = 0;    // glider to core
  float coreOffsetM = 0;      // circle centre to core: how badly the circle is placed
  uint8_t confidence = 0;     // 0..100

  // The circle centre, same frame, so the page can draw what the pilot is currently flying.
  float centerRightM = 0;
  float centerAheadM = 0;

  // --- cues -----------------------------------------------------------------------------
  bool centred = false;  // the circle is on the core; nothing to do but keep turning
  bool openNow = false;  // pointing at the core and offset: flatten the turn now

  // --- numbers for the top of the page --------------------------------------------------
  bool avgClimbValid = false;
  int16_t avgClimbCms = 0;  // recency-weighted average over the last half minute

  // --- the trail ------------------------------------------------------------------------
  uint8_t trailCount = 0;
  ThermalCoreTrailPoint trail[THERMAL_CORE_MAX_TRAIL];
};

class ThermalCore {
 public:
  void reset();
  void update();
  const ThermalCoreState& state() const { return state_; }

 private:
  // Circling is entered and left on separate thresholds: a turn has to be sustained to count,
  // and has to have properly stopped before the page abandons the layout.  Without the
  // hysteresis a single ragged fix flips the whole display.
  static constexpr float ENTER_TURN_RATE_DEG_S = 8.0f;
  static constexpr float LEAVE_TURN_RATE_DEG_S = 4.0f;
  static constexpr float ENTER_HOLD_S = 4.0f;
  static constexpr float LEAVE_HOLD_S = 3.0f;
  static constexpr float ENTER_SWEEP_DEG = 200.0f;

  static constexpr float CORE_SMOOTH_TAU_S = 5.0f;
  static constexpr float SCALE_SMOOTH_TAU_S = 3.0f;
  // Zoomed out while gliding, because what matters between climbs is where the lift was on the
  // way in, and at 11 m/s a tight scale shows about eight seconds of it.
  static constexpr float GLIDE_SCALE_RADIUS_M = 80.0f;

  // Close enough that chasing it further would cost more than it gains.
  static constexpr float CENTRED_M = 8.0f;
  // How near the nose has to be to the line from the circle centre to the core before flattening
  // the turn is the right move.
  static constexpr float OPEN_CONE_DEG = 30.0f;

  // Shown at the first threshold and kept until the second.  Confidence sits right on the line
  // for whole laps at a time when the circle is nearly centred, and a marker that blinks in and
  // out once a second is worse than one that is a little stale.
  static constexpr uint8_t SHOW_CONFIDENCE = 25;
  static constexpr uint8_t KEEP_CONFIDENCE = 15;
  static constexpr float AVERAGE_WINDOW_S = 30.0f;
  static constexpr float MAX_USABLE_WIND_MPS = 20.0f;

  bool sampleSensors(float& nowS, thermal::Vec2& groundPosM, float& headingRad);
  void updateCircling(const thermal::CircleFit& fit, float dtS);
  void updateCore(const thermal::CoreSolution& solution, float nowS, thermal::Vec2 groundPosM,
                  float dtS);
  void buildTrail(float nowS, thermal::Vec2 groundPosM, float headingRad,
                  const thermal::CoreSolution& solution);
  void updateAverageClimb(float nowS);

  thermal::CoreSolver solver_;
  ThermalCoreState state_;

  bool hasLastFix_ = false;
  uint32_t lastFixMillis_ = 0;
  double lastFixLat_ = 0;
  double lastFixLon_ = 0;
  thermal::Vec2 lastFixPosM_;
  thermal::Vec2 groundVelMps_;
  thermal::Vec2 windToMps_;
  bool windValid_ = false;

  float lastUpdateS_ = 0;
  bool hasLastUpdate_ = false;
  float turnHoldS_ = 0;
  float straightHoldS_ = 0;

  bool hasSmoothedCore_ = false;
  thermal::Vec2 smoothedCoreM_;  // local ground frame, valid at smoothedCoreS_
  float smoothedCoreS_ = 0;
};

extern ThermalCore thermalCore;
