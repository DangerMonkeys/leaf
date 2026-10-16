#pragma once

#include <stdint.h>

namespace thermal_page {
  constexpr uint8_t VARIO_BAR_TOP = 11;
  constexpr uint8_t VARIO_BAR_WIDTH = 17;
  constexpr uint8_t VARIO_BAR_HALF_HEIGHT = 30;
  constexpr uint8_t CLIMB_BOX_X = VARIO_BAR_WIDTH;
  constexpr uint8_t CLIMB_UNITS_CENTER_X = CLIMB_BOX_X + 9;
  constexpr uint8_t CLIMB_BOX_HEIGHT = 14;
  constexpr uint8_t CLIMB_BOX_Y = VARIO_BAR_TOP + VARIO_BAR_HALF_HEIGHT - CLIMB_BOX_HEIGHT / 2;

  void drawVario(int32_t climbRate);
  void drawClimbRate(int32_t climbRate, bool unitsBelow = false);
}  // namespace thermal_page
