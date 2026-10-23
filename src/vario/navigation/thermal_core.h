#pragma once

#include <Arduino.h>

#include "navigation/thermal_climb_history.h"
#include "navigation/thermal_core_target.h"
#include "navigation/thermal_tracker.h"

constexpr uint8_t THERMAL_CORE_MAX_MARKERS = ThermalTracker::MAX_DETECTOR_SAMPLES;

enum class ThermalCoreMarkerGlyph : uint8_t {
  Cross3 = 0,
  Ring5 = 1,
  Ring7 = 2,
  Ring9 = 3,
  Ring11 = 4,
  FilledRing11 = 5,
};

struct ThermalCoreMarker {
  bool visible = false;
  int16_t x = 0;
  int16_t y = 0;
  ThermalCoreMarkerGlyph glyph = ThermalCoreMarkerGlyph::Cross3;
};

struct ThermalCoreEstimate {
  bool valid = false;
  int8_t direction = 0;
  int8_t adviceQ7 = 0;
  uint8_t markerCount = 0;
  ThermalCoreMarker markers[THERMAL_CORE_MAX_MARKERS];
  bool centerValid = false;
  int16_t centerX = 0;
  int16_t centerY = 0;
  int16_t mapCourseDeciDeg = 0;
};

class ThermalCore {
 public:
  static constexpr float MAP_METERS_PER_PX = 1.15f;

  void reset();
  void update();
  const ThermalCoreEstimate& estimate() const { return estimate_; }
  const ThermalClimbHistory& climbHistory() const { return climbHistory_; }

 private:
  void updateTurnSegment(const ThermalTracker::CoreSample& latest,
                         const ThermalTracker::CoreSample& prior);
  ThermalCoreEstimate estimate_;
  bool turnGuidanceActive_ = false;
  bool turnGuidancePreviouslyActive_ = false;
  uint32_t lastGuidanceSampleMs_ = 0;
  uint32_t fitStartMs_ = 0;
  uint32_t fitStraightMs_ = 0;
  uint32_t fitReverseStartMs_ = 0;
  uint16_t fitReverseDeg_ = 0;
  int8_t fitDirection_ = 0;
  bool episodeClimbScaleValid_ = false;
  int16_t episodeMaxClimbCms_ = 0;
  ThermalCoreTarget target_;
  ThermalClimbHistory climbHistory_;
};

extern ThermalCore thermalCore;
