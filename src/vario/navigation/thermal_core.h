#pragma once

#include <Arduino.h>

#include "navigation/thermal_tracker.h"

constexpr uint8_t THERMAL_CORE_MAX_MARKERS = ThermalTracker::MAX_DETECTOR_SAMPLES;

enum class ThermalCoreMarkerGlyph : uint8_t {
  Cross3 = 0,
  Ring5 = 1,
  Ring7Thick = 2,
  Ring9Thick = 3,
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
};

class ThermalCore {
 public:
  void reset();
  void update();
  const ThermalCoreEstimate& estimate() const { return estimate_; }

 private:
  ThermalCoreEstimate estimate_;
};

extern ThermalCore thermalCore;
