#pragma once

#include <stdint.h>

namespace thermal_page {
  constexpr uint8_t VARIO_BAR_TOP = 11;
  constexpr uint8_t VARIO_BAR_WIDTH = 17;
  constexpr uint8_t CORE_VARIO_BAR_WIDTH = VARIO_BAR_WIDTH - 3;
  constexpr uint8_t CORE_CLIMB_BOX_WIDTH = 41;
  constexpr uint8_t CORE_CLIMB_BOX_TOP = 35;
  constexpr uint8_t CORE_CLIMB_BOX_HEIGHT = 16;
  constexpr uint8_t VARIO_BAR_HALF_HEIGHT = 30;
  constexpr uint8_t CLIMB_BOX_X = VARIO_BAR_WIDTH;
  constexpr uint8_t CLIMB_UNITS_CENTER_X = CLIMB_BOX_X + 9;
  constexpr uint8_t CLIMB_BOX_HEIGHT = 14;
  constexpr uint8_t CLIMB_BOX_Y = VARIO_BAR_TOP + VARIO_BAR_HALF_HEIGHT - CLIMB_BOX_HEIGHT / 2;

  void drawVario(int32_t climbRate, uint8_t topOffset = 0, uint8_t width = VARIO_BAR_WIDTH);
  void drawClimbRate(int32_t climbRate, bool unitsBelow = false);
  void drawCoreClimbRate(int32_t climbRate);
  void drawCoreClimbAverage(int32_t climbRate, uint8_t x, uint8_t top);
  uint8_t drawCoreAltitude(int32_t altitude, uint8_t x, uint8_t baselineY,
                           uint8_t* tensX = nullptr);
}  // namespace thermal_page
