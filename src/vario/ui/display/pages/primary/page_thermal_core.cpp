#include "ui/display/pages/primary/page_thermal_core.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>

#include "hardware/buttons.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logging/log.h"
#include "navigation/thermal_core.h"
#include "power.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/utils.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

namespace {
  constexpr int16_t MAP_LEFT = 0;
  constexpr int16_t MAP_TOP = 71;
  constexpr int16_t MAP_WIDTH = 96;
  constexpr int16_t MAP_HEIGHT = 104;
  // Expanding the top edge must not shift the aircraft or breadcrumb projection.
  constexpr int16_t MAP_CENTER_Y = 124;
  constexpr int16_t GUIDANCE_CENTER_X = MAP_LEFT + MAP_WIDTH / 2;
  constexpr int16_t GUIDANCE_CENTER_Y = MAP_TOP + 11;
  constexpr int16_t AIRCRAFT_Y = MAP_CENTER_Y - 9;
  constexpr int16_t LEFT_AIRCRAFT_X = 20;
  constexpr int16_t RIGHT_AIRCRAFT_X = 76;
  constexpr int16_t CENTER_AIRCRAFT_X = 48;
  constexpr int16_t TARGET_RADIUS = 38;
  constexpr int16_t TARGET_SHIFT_MAX = 19;
  constexpr uint8_t VARIO_BAR_TOP = 11;
  constexpr uint8_t VARIO_BAR_WIDTH = 17;
  constexpr uint8_t VARIO_BAR_HALF_HEIGHT = 30;
  constexpr uint8_t ALT_X = 37;
  constexpr uint8_t ALT_VALUE_BASELINE_Y = 69;
  constexpr uint8_t ALT_TYPE_X = VARIO_BAR_WIDTH + 1;
  constexpr uint8_t ALT_UNITS_X = ALT_TYPE_X + 8;
  constexpr uint8_t ALT_TYPE_BASELINE_Y = ALT_VALUE_BASELINE_Y - 1;
  constexpr uint8_t ALT_UNITS_BASELINE_Y = ALT_TYPE_BASELINE_Y - 8;
  constexpr uint8_t CLIMB_BOX_X = VARIO_BAR_WIDTH;
  constexpr uint8_t CLIMB_UNITS_CENTER_X = CLIMB_BOX_X + 9;
  constexpr uint8_t CLIMB_BOX_HEIGHT = 14;
  constexpr uint8_t CLIMB_BOX_Y = VARIO_BAR_TOP + VARIO_BAR_HALF_HEIGHT - CLIMB_BOX_HEIGHT / 2;
  constexpr uint8_t AIRCRAFT_GLYPH_WIDTH = 19;
  constexpr uint8_t AIRCRAFT_GLYPH_HEIGHT = 19;
  constexpr uint32_t AIRCRAFT_BLACK[AIRCRAFT_GLYPH_HEIGHT] = {
      0x00000, 0x00200, 0x00700, 0x00700, 0x00F80, 0x00D80, 0x018C0, 0x018C0, 0x03060, 0x03060,
      0x06030, 0x06030, 0x0C018, 0x0C018, 0x19FCC, 0x1FFFC, 0x3F07E, 0x3800E, 0x00000,
  };
  constexpr uint32_t AIRCRAFT_WHITE[AIRCRAFT_GLYPH_HEIGHT] = {
      0x00700, 0x00D80, 0x00880, 0x018C0, 0x01040, 0x03260, 0x02720, 0x06730, 0x04F90, 0x0CF98,
      0x09FC8, 0x19FCC, 0x13FE4, 0x33FE6, 0x26032, 0x60003, 0x40001, 0x40001, 0x7800F,
  };

  enum ThermalCorePageItem : uint8_t {
    cursor_thermalCorePage_none,
    cursor_thermalCorePage_alt,
    cursor_thermalCorePage_timer,
  };
  constexpr uint8_t THERMAL_CORE_CURSOR_MAX = cursor_thermalCorePage_timer;
  constexpr uint8_t THERMAL_CORE_CURSOR_TIMEOUT = 8;
  int8_t thermalCorePageCursor = cursor_thermalCorePage_none;
  uint8_t thermalCorePageCursorTimeCount = 0;

  bool thermalCorePageVolumeShortcut(Button button, ButtonEvent state) {
    if (!settings.volumeShortcut || (button != Button::UP && button != Button::DOWN) ||
        state != ButtonEvent::INCREMENTED) {
      return false;
    }

    if (!settings.adjustShortcutVolume(button)) buttons.consumeButton();
    return true;
  }

  void thermalCorePageCursorMove(Button button) {
    if (button == Button::UP) {
      thermalCorePageCursor--;
      if (thermalCorePageCursor < 0) thermalCorePageCursor = THERMAL_CORE_CURSOR_MAX;
    }
    if (button == Button::DOWN) {
      thermalCorePageCursor++;
      if (thermalCorePageCursor > THERMAL_CORE_CURSOR_MAX) thermalCorePageCursor = 0;
    }
    speaker.playSound(thermalCorePageCursor == cursor_thermalCorePage_none ? fx::doubleClick
                                                                           : fx::click);
  }

  void stampCross3(int16_t x, int16_t y) {
    u8g2.drawVLine(x, y - 1, 3);
    u8g2.drawHLine(x - 1, y, 3);
  }

  void stampRing5(int16_t x, int16_t y) {
    u8g2.drawHLine(x - 1, y - 2, 3);
    u8g2.drawPixel(x - 2, y - 1);
    u8g2.drawPixel(x + 2, y - 1);
    u8g2.drawPixel(x - 2, y);
    u8g2.drawPixel(x + 2, y);
    u8g2.drawPixel(x - 2, y + 1);
    u8g2.drawPixel(x + 2, y + 1);
    u8g2.drawHLine(x - 1, y + 2, 3);
  }

  void stampRing7(int16_t x, int16_t y) {
    u8g2.drawHLine(x - 1, y - 3, 3);
    u8g2.drawHLine(x - 2, y - 2, 2);
    u8g2.drawHLine(x + 1, y - 2, 2);
    u8g2.drawHLine(x - 3, y - 1, 2);
    u8g2.drawHLine(x + 2, y - 1, 2);
    u8g2.drawPixel(x - 3, y);
    u8g2.drawPixel(x + 3, y);
    u8g2.drawHLine(x - 3, y + 1, 2);
    u8g2.drawHLine(x + 2, y + 1, 2);
    u8g2.drawHLine(x - 2, y + 2, 2);
    u8g2.drawHLine(x + 1, y + 2, 2);
    u8g2.drawHLine(x - 1, y + 3, 3);
  }

  void stampRing9(int16_t x, int16_t y) {
    u8g2.drawHLine(x - 1, y - 4, 3);
    u8g2.drawHLine(x - 3, y - 3, 7);
    u8g2.drawHLine(x - 3, y - 2, 2);
    u8g2.drawHLine(x + 2, y - 2, 2);
    u8g2.drawHLine(x - 4, y - 1, 2);
    u8g2.drawHLine(x + 3, y - 1, 2);
    u8g2.drawHLine(x - 4, y, 2);
    u8g2.drawHLine(x + 3, y, 2);
    u8g2.drawHLine(x - 4, y + 1, 2);
    u8g2.drawHLine(x + 3, y + 1, 2);
    u8g2.drawHLine(x - 3, y + 2, 2);
    u8g2.drawHLine(x + 2, y + 2, 2);
    u8g2.drawHLine(x - 3, y + 3, 7);
    u8g2.drawHLine(x - 1, y + 4, 3);
  }

  void stampRing11(int16_t x, int16_t y) {
    u8g2.drawHLine(x - 1, y - 5, 3);
    u8g2.drawHLine(x - 3, y - 4, 7);
    u8g2.drawHLine(x - 4, y - 3, 9);
    u8g2.drawHLine(x - 4, y - 2, 3);
    u8g2.drawHLine(x + 2, y - 2, 3);
    u8g2.drawHLine(x - 5, y - 1, 3);
    u8g2.drawHLine(x + 3, y - 1, 3);
    u8g2.drawHLine(x - 5, y, 3);
    u8g2.drawHLine(x + 3, y, 3);
    u8g2.drawHLine(x - 5, y + 1, 3);
    u8g2.drawHLine(x + 3, y + 1, 3);
    u8g2.drawHLine(x - 4, y + 2, 3);
    u8g2.drawHLine(x + 2, y + 2, 3);
    u8g2.drawHLine(x - 4, y + 3, 9);
    u8g2.drawHLine(x - 3, y + 4, 7);
    u8g2.drawHLine(x - 1, y + 5, 3);
  }

  void stampFilledRing11(int16_t x, int16_t y) {
    u8g2.drawHLine(x - 1, y - 5, 3);
    u8g2.drawHLine(x - 3, y - 4, 7);
    u8g2.drawHLine(x - 4, y - 3, 9);
    u8g2.drawHLine(x - 4, y - 2, 9);
    u8g2.drawHLine(x - 5, y - 1, 5);
    u8g2.drawHLine(x + 1, y - 1, 5);
    u8g2.drawHLine(x - 5, y, 4);
    u8g2.drawHLine(x + 2, y, 4);
    u8g2.drawHLine(x - 5, y + 1, 5);
    u8g2.drawHLine(x + 1, y + 1, 5);
    u8g2.drawHLine(x - 4, y + 2, 9);
    u8g2.drawHLine(x - 4, y + 3, 9);
    u8g2.drawHLine(x - 3, y + 4, 7);
    u8g2.drawHLine(x - 1, y + 5, 3);
  }

  uint8_t markerRadius(const ThermalCoreMarker& marker) {
    switch (marker.glyph) {
      case ThermalCoreMarkerGlyph::Cross3:
        return 1;
      case ThermalCoreMarkerGlyph::Ring5:
        return 2;
      case ThermalCoreMarkerGlyph::Ring7:
        return 3;
      case ThermalCoreMarkerGlyph::Ring9:
        return 4;
      case ThermalCoreMarkerGlyph::Ring11:
      case ThermalCoreMarkerGlyph::FilledRing11:
        return 5;
    }
    return 5;
  }

  bool markerFitsMap(const ThermalCoreMarker& marker) {
    const uint8_t radius = markerRadius(marker);
    return marker.x - radius > MAP_LEFT && marker.x + radius < MAP_LEFT + MAP_WIDTH - 1 &&
           marker.y - radius > MAP_TOP && marker.y + radius < MAP_TOP + MAP_HEIGHT - 1;
  }

  void drawMarker(const ThermalCoreMarker& marker) {
    if (!marker.visible) return;
    if (!markerFitsMap(marker)) return;
    switch (marker.glyph) {
      case ThermalCoreMarkerGlyph::Cross3:
        stampCross3(marker.x, marker.y);
        break;
      case ThermalCoreMarkerGlyph::Ring5:
        stampRing5(marker.x, marker.y);
        break;
      case ThermalCoreMarkerGlyph::Ring7:
        stampRing7(marker.x, marker.y);
        break;
      case ThermalCoreMarkerGlyph::Ring9:
        stampRing9(marker.x, marker.y);
        break;
      case ThermalCoreMarkerGlyph::Ring11:
        stampRing11(marker.x, marker.y);
        break;
      case ThermalCoreMarkerGlyph::FilledRing11:
        stampFilledRing11(marker.x, marker.y);
        break;
    }
  }

  void drawAircraft(int16_t x, int16_t y) {
    const int16_t x0 = x - AIRCRAFT_GLYPH_WIDTH / 2;
    const int16_t y0 = y - AIRCRAFT_GLYPH_HEIGHT / 2;

    u8g2.setDrawColor(0);
    for (uint8_t row = 0; row < AIRCRAFT_GLYPH_HEIGHT; ++row) {
      for (uint8_t col = 0; col < AIRCRAFT_GLYPH_WIDTH; ++col) {
        if (AIRCRAFT_WHITE[row] & (1UL << col)) u8g2.drawPixel(x0 + col, y0 + row);
      }
    }

    u8g2.setDrawColor(1);
    for (uint8_t row = 0; row < AIRCRAFT_GLYPH_HEIGHT; ++row) {
      for (uint8_t col = 0; col < AIRCRAFT_GLYPH_WIDTH; ++col) {
        if (AIRCRAFT_BLACK[row] & (1UL << col)) u8g2.drawPixel(x0 + col, y0 + row);
      }
    }
  }

  void drawTargetScale(int16_t cx, int16_t cy) {
    const int16_t left = cx - 25;
    const int16_t right = cx + 25;
    const int16_t top = cy - 10;
    const int16_t bottom = cy + 10;
    const int16_t innerTop = cy - 4;
    const int16_t innerBottom = cy + 4;

    u8g2.setDrawColor(0);
    u8g2.drawBox(left, innerTop, 51, 9);
    u8g2.drawBox(cx - 5, top, 11, 21);
    u8g2.setDrawColor(1);

    u8g2.drawLine(left, innerTop, cx - 5, innerTop);
    u8g2.drawLine(cx - 5, innerTop, cx - 5, top);
    u8g2.drawLine(cx - 5, top, cx + 5, top);
    u8g2.drawLine(cx + 5, top, cx + 5, innerTop);
    u8g2.drawLine(cx + 5, innerTop, right, innerTop);
    u8g2.drawLine(right, innerTop, right, innerBottom);
    u8g2.drawLine(right, innerBottom, cx + 5, innerBottom);
    u8g2.drawLine(cx + 5, innerBottom, cx + 5, bottom);
    u8g2.drawLine(cx + 5, bottom, cx - 5, bottom);
    u8g2.drawLine(cx - 5, bottom, cx - 5, innerBottom);
    u8g2.drawLine(cx - 5, innerBottom, left, innerBottom);
    u8g2.drawLine(left, innerBottom, left, innerTop);
  }

  void drawAltitudeField() {
    const uint8_t altType = settings.disp_thmPageAltType == altType_MSL ? altType_MSL : altType_GPS;
    u8g2.setFont(leaf_labels);
    u8g2.setCursor(ALT_TYPE_X, ALT_TYPE_BASELINE_Y);
    print_alt_label(altType);
    u8g2.setCursor(ALT_UNITS_X, ALT_UNITS_BASELINE_Y);
    u8g2.print(settings.units_alt ? "ft" : "m");

    const int32_t altitudeCm = altType == altType_MSL
                                   ? baro.altAdjusted()
                                   : static_cast<int32_t>(100 * gps.altitude.meters());
    const int32_t altitude = settings.units_alt ? altitudeCm * 100 / 3048 : altitudeCm / 100;
    if (altitude >= 10000 || (altitude <= -1000 && altitude >= -9999)) {
      // Wide values need tighter spacing to fit the new position without clipping digits.
      const int32_t magnitude = min<int32_t>(abs(altitude), 99999);
      char value[12];
      snprintf(value, sizeof(value), "%s%ld,%03ld", altitude < 0 ? "-" : "",
               static_cast<long>(magnitude / 1000), static_cast<long>(magnitude % 1000));
      u8g2.setFont(leaf_10x17);
      uint8_t x = ALT_X;
      for (uint8_t i = 0; value[i]; ++i) {
        x += u8g2.drawGlyph(x, ALT_VALUE_BASELINE_Y, value[i]) - 1;
      }
    } else {
      display_alt_type(ALT_X, ALT_VALUE_BASELINE_Y, leaf_10x17, altType);
    }

    if (thermalCorePageCursor == cursor_thermalCorePage_alt) {
      display_selectionBox(ALT_X - 1, ALT_VALUE_BASELINE_Y - 18, 96 - (ALT_X - 1), 19, 7);
    }
  }

  void drawClimbRateField(int32_t climbRate) {
    u8g2.setFont(leaf_5h);
    const char* units = settings.units_climb ? "fpm" : "m/s";
    u8g2.drawStr(CLIMB_UNITS_CENTER_X - (u8g2.getStrWidth(units) + 1) / 2, CLIMB_BOX_Y - 2, units);

    const uint8_t zeroY = VARIO_BAR_TOP + VARIO_BAR_HALF_HEIGHT;
    u8g2.setFont(leaf_5x8);
    u8g2.setFontMode(1);
    u8g2.setDrawColor(0);
    u8g2.drawGlyph(CLIMB_BOX_X - 6, zeroY + 5, climbRate < 0 ? '-' : '+');
    u8g2.setFontMode(0);
    u8g2.setDrawColor(1);

    // The triangle shows the sign; the box shows the magnitude.
    climbRate = abs(climbRate);
    char value[12];
    const uint8_t* digitFonts[12];
    if (settings.units_climb) {
      // Keep the same ten-fpm resolution as the other flight pages.
      const int32_t fpm = min<int32_t>(climbRate * 197 / 1000 * 10, 9999);
      snprintf(value, sizeof(value), fpm < 1000 ? "%03ld" : "%04ld", static_cast<long>(fpm));
      digitFonts[0] = leaf_6x12;
      digitFonts[1] = leaf_6x10;
      digitFonts[2] = fpm < 1000 ? leaf_6x10 : leaf_5x8;
      digitFonts[3] = leaf_5x8;
    } else {
      const int32_t tenths = min<int32_t>((climbRate + 5) / 10, 999);
      snprintf(value, sizeof(value), "%ld.%ld", static_cast<long>(tenths / 10),
               static_cast<long>(tenths % 10));
      for (uint8_t i = 0; value[i]; ++i) digitFonts[i] = leaf_6x12;
    }

    int16_t width = 0;
    uint8_t digitWidths[12];
    for (uint8_t i = 0; value[i]; ++i) {
      u8g2.setFont(digitFonts[i]);
      digitWidths[i] = u8g2_GetGlyphWidth(u8g2.getU8g2(), value[i]);
      if (value[i + 1]) {
        width += digitWidths[i];
      } else {
        // Exclude the final glyph's advance spacing for an exact one-pixel right margin.
        const u8g2_t* fontState = u8g2.getU8g2();
        width += fontState->glyph_x_offset + fontState->font_decode.glyph_width;
      }
    }
    u8g2.drawBox(CLIMB_BOX_X, CLIMB_BOX_Y, width + 1, CLIMB_BOX_HEIGHT);
    int16_t x = CLIMB_BOX_X;
    u8g2.setDrawColor(0);
    for (uint8_t i = 0; value[i]; ++i) {
      u8g2.setFont(digitFonts[i]);
      const uint8_t height = digitFonts[i] == leaf_6x12 ? 12 : digitFonts[i] == leaf_6x10 ? 10 : 8;
      u8g2.drawGlyph(x, CLIMB_BOX_Y + height + 1, value[i]);
      x += digitWidths[i];
    }
    u8g2.setDrawColor(1);
  }

  void drawGuidanceLine(int16_t cx, int16_t cy) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(cx - 2, cy - 8, 6, 16);
    u8g2.setDrawColor(1);
    u8g2.drawBox(cx - 1, cy - 7, 4, 14);
  }

  void drawSmallGuidanceArrow(int16_t lineLeft, int16_t cy, int8_t direction) {
    const int16_t baseX = direction > 0 ? lineLeft + 5 : lineLeft - 2;
    u8g2.drawPixel(baseX, cy - 2);
    u8g2.drawPixel(baseX, cy + 2);
    u8g2.drawHLine(direction > 0 ? baseX : baseX - 1, cy - 1, 2);
    u8g2.drawHLine(direction > 0 ? baseX : baseX - 1, cy + 1, 2);
    u8g2.drawHLine(direction > 0 ? baseX : baseX - 2, cy, 3);
  }

  void drawWideGuidanceArrow(int16_t baseX, int16_t cy, int8_t direction) {
    u8g2.drawHLine(direction > 0 ? baseX : baseX - 1, cy - 2, 2);
    u8g2.drawHLine(direction > 0 ? baseX : baseX - 1, cy + 2, 2);
    u8g2.drawHLine(direction > 0 ? baseX : baseX - 3, cy - 1, 4);
    u8g2.drawHLine(direction > 0 ? baseX : baseX - 3, cy + 1, 4);
    u8g2.drawHLine(direction > 0 ? baseX : baseX - 5, cy, 6);
  }

  void drawGuidanceReticle(int16_t cx, int16_t cy, int16_t neutralCx) {
    drawGuidanceLine(cx, cy);

    const int16_t error = neutralCx - cx;
    const int16_t distance = abs(error);
    if (distance <= 2) return;

    const int8_t direction = error > 0 ? 1 : -1;
    const int16_t lineLeft = cx - 1;
    if (distance <= 6) {
      drawSmallGuidanceArrow(lineLeft, cy, direction);
      return;
    }

    const int16_t firstBaseX = direction > 0 ? lineLeft + 5 : lineLeft - 2;
    drawWideGuidanceArrow(firstBaseX, cy, direction);
    if (distance >= 14) drawWideGuidanceArrow(firstBaseX + direction * 7, cy, direction);
  }

  void toggleAltitudeType() {
    if (settings.disp_thmPageAltType == altType_MSL)
      settings.disp_thmPageAltType = altType_GPS;
    else
      settings.disp_thmPageAltType = altType_MSL;
    speaker.playSound(fx::neutral);
  }

  void drawThermalCoreContent() {
    const int32_t climbRate = baro.climbRateFilteredValid() ? baro.climbRateFiltered() : 0;
    const int32_t displayClimbRate = baro.climbRateFilteredValid() ? baro.climbRateForDisplay() : 0;
    const ThermalCoreEstimate& estimate = thermalCore.estimate();
    const int8_t direction = estimate.direction;
    const int8_t turnSide = direction < 0 ? -1 : direction > 0 ? 1 : 0;
    const int16_t aircraftX = direction < 0   ? RIGHT_AIRCRAFT_X
                              : direction > 0 ? LEFT_AIRCRAFT_X
                                              : CENTER_AIRCRAFT_X;
    const int16_t adviceShift = estimate.valid ? (estimate.adviceQ7 * TARGET_SHIFT_MAX) / 127 : 0;
    const int16_t dynamicRadius =
        min<int16_t>(TARGET_RADIUS + TARGET_SHIFT_MAX,
                     max<int16_t>(TARGET_RADIUS - TARGET_SHIFT_MAX, TARGET_RADIUS + adviceShift));
    const int16_t targetCx = turnSide == 0 ? aircraftX : aircraftX + turnSide * dynamicRadius;
    const bool hasTurn = turnSide != 0;
    const bool hasGuidance = estimate.valid;

    display_varioBar(VARIO_BAR_TOP, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_WIDTH,
                     climbRate);
    display_climbRatePointerTriangle(VARIO_BAR_WIDTH, VARIO_BAR_TOP + VARIO_BAR_HALF_HEIGHT, 8);
    display_windSockRing(61, 29, 12, 7, true);
    drawAltitudeField();
    drawClimbRateField(displayClimbRate);

    for (uint8_t i = 0; i < estimate.markerCount; ++i) {
      drawMarker(estimate.markers[i]);
    }

    const int16_t neutralTargetCx =
        hasTurn ? aircraftX + turnSide * TARGET_RADIUS : CENTER_AIRCRAFT_X;
    drawAircraft(aircraftX, AIRCRAFT_Y);
    u8g2.drawFrame(MAP_LEFT, MAP_TOP, MAP_WIDTH, MAP_HEIGHT);

    if (hasTurn) drawTargetScale(GUIDANCE_CENTER_X, GUIDANCE_CENTER_Y);
    if (hasGuidance) {
      const int16_t guidanceCx = GUIDANCE_CENTER_X + targetCx - neutralTargetCx;
      drawGuidanceReticle(guidanceCx, GUIDANCE_CENTER_Y, GUIDANCE_CENTER_X);
    }
  }
}  // namespace

void thermalCorePage_draw() {
  if (thermalCorePageCursor != cursor_thermalCorePage_none &&
      thermalCorePageCursorTimeCount++ >= THERMAL_CORE_CURSOR_TIMEOUT) {
    thermalCorePageCursor = cursor_thermalCorePage_none;
    thermalCorePageCursorTimeCount = 0;
  }

  u8g2.firstPage();
  do {
    display_headerAndFooter(thermalCorePageCursor == cursor_thermalCorePage_timer, false);
    drawThermalCoreContent();
  } while (u8g2.nextPage());
}

void thermalCorePage_button(Button button, ButtonEvent state, uint8_t count) {
  thermalCorePageCursorTimeCount = 0;

  switch (thermalCorePageCursor) {
    case cursor_thermalCorePage_none:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (thermalCorePageVolumeShortcut(button, state)) {
            break;
          } else if (state == ButtonEvent::CLICKED) {
            thermalCorePageCursorMove(button);
          }
          break;
        case Button::RIGHT:
          if (state == ButtonEvent::CLICKED) {
            display.turnPage(PageAction::Next);
            speaker.playSound(fx::increase);
          }
          break;
        case Button::LEFT:
          if (state == ButtonEvent::CLICKED) {
            display.turnPage(PageAction::Prev);
            speaker.playSound(fx::decrease);
          }
          break;
        case Button::CENTER:
          if (state == ButtonEvent::INCREMENTED && count == 2) {
            power.shutdown();
            return;
          }
          break;
      }
      break;
    case cursor_thermalCorePage_alt:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) thermalCorePageCursorMove(button);
          break;
        case Button::LEFT:
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED) toggleAltitudeType();
          break;
      }
      break;
    case cursor_thermalCorePage_timer:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) thermalCorePageCursorMove(button);
          break;
        case Button::LEFT:
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED && !flightTimer_isRunning()) {
            flightTimer_start();
            thermalCorePageCursor = cursor_thermalCorePage_none;
          } else if (state == ButtonEvent::HELD && flightTimer_isRunning()) {
            buttons.consumeButton();
            flightTimer_stop();
            thermalCorePageCursor = cursor_thermalCorePage_none;
          }
          break;
      }
      break;
  }
  display.update();
}
