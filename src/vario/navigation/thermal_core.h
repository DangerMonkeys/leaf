#pragma once

#include <Arduino.h>

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

 private:
  ThermalCoreEstimate estimate_;
  bool turnGuidanceActive_ = false;
  bool turnGuidancePreviouslyActive_ = false;
  int8_t activeTurnDirection_ = 0;
  uint32_t straightDurationMs_ = 0;
  uint32_t lastGuidanceSampleMs_ = 0;
  uint32_t lastSustainedTurnMs_ = 0;
  bool episodeClimbScaleValid_ = false;
  int16_t episodeMaxClimbCms_ = 0;
  bool centerValid_ = false;
  float centerEastM_ = 0;
  float centerNorthM_ = 0;
  float centerAltitudeM_ = 0;
  float centerSlopeEast_ = 0;
  float centerSlopeNorth_ = 0;
  uint32_t lastCenterSampleMs_ = 0;
};

extern ThermalCore thermalCore;
