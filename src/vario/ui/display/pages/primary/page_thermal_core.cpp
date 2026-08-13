#include "ui/display/pages/primary/page_thermal_core.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>

#include "hardware/buttons.h"
#include "instruments/baro.h"
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
  constexpr int16_t MAP_TOP = 76;
  constexpr int16_t MAP_SIZE = 96;
  constexpr int16_t AIRCRAFT_Y = MAP_TOP + MAP_SIZE / 2;
  constexpr int16_t LEFT_AIRCRAFT_X = 20;
  constexpr int16_t RIGHT_AIRCRAFT_X = 76;
  constexpr int16_t CENTER_AIRCRAFT_X = 48;
  constexpr int16_t TARGET_RADIUS = 38;
  constexpr int16_t TARGET_SHIFT_MAX = 14;
  constexpr uint8_t VARIO_BAR_TOP = 16;
  constexpr uint8_t VARIO_BAR_WIDTH = 17;
  constexpr uint8_t VARIO_BAR_HALF_HEIGHT = 30;
  constexpr uint8_t ALT_LABEL_X = VARIO_BAR_WIDTH + 2;
  constexpr uint8_t ALT_X = 28;
  constexpr uint8_t ALT_BASELINE_Y = 39;
  constexpr uint8_t CLIMB_X = 30;
  constexpr uint8_t CLIMB_BASELINE_Y = 63;

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

  void stampRing7Thick(int16_t x, int16_t y) {
    u8g2.drawHLine(x - 1, y - 3, 3);
    u8g2.drawHLine(x - 2, y - 2, 5);
    u8g2.drawHLine(x - 3, y - 1, 3);
    u8g2.drawHLine(x + 1, y - 1, 3);
    u8g2.drawHLine(x - 3, y, 2);
    u8g2.drawHLine(x + 2, y, 2);
    u8g2.drawHLine(x - 3, y + 1, 3);
    u8g2.drawHLine(x + 1, y + 1, 3);
    u8g2.drawHLine(x - 2, y + 2, 5);
    u8g2.drawHLine(x - 1, y + 3, 3);
  }

  void stampRing9Thick(int16_t x, int16_t y) {
    u8g2.drawHLine(x - 2, y - 4, 5);
    u8g2.drawHLine(x - 3, y - 3, 7);
    u8g2.drawHLine(x - 4, y - 2, 3);
    u8g2.drawHLine(x + 2, y - 2, 3);
    u8g2.drawHLine(x - 4, y - 1, 2);
    u8g2.drawHLine(x + 3, y - 1, 2);
    u8g2.drawHLine(x - 4, y, 2);
    u8g2.drawHLine(x + 3, y, 2);
    u8g2.drawHLine(x - 4, y + 1, 2);
    u8g2.drawHLine(x + 3, y + 1, 2);
    u8g2.drawHLine(x - 4, y + 2, 3);
    u8g2.drawHLine(x + 2, y + 2, 3);
    u8g2.drawHLine(x - 3, y + 3, 7);
    u8g2.drawHLine(x - 2, y + 4, 5);
  }

  uint8_t markerRadius(const ThermalCoreMarker& marker) {
    switch (marker.glyph) {
      case ThermalCoreMarkerGlyph::Cross3:
        return 1;
      case ThermalCoreMarkerGlyph::Ring5:
        return 2;
      case ThermalCoreMarkerGlyph::Ring7Thick:
        return 3;
      case ThermalCoreMarkerGlyph::Ring9Thick:
        return 4;
    }
    return 4;
  }

  bool markerFitsMap(const ThermalCoreMarker& marker) {
    const uint8_t radius = markerRadius(marker);
    return marker.x - radius > MAP_LEFT && marker.x + radius < MAP_LEFT + MAP_SIZE - 1 &&
           marker.y - radius > MAP_TOP && marker.y + radius < MAP_TOP + MAP_SIZE - 1;
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
      case ThermalCoreMarkerGlyph::Ring7Thick:
        stampRing7Thick(marker.x, marker.y);
        break;
      case ThermalCoreMarkerGlyph::Ring9Thick:
        stampRing9Thick(marker.x, marker.y);
        break;
    }
  }

  void drawAircraft(int16_t x, int16_t y) {
    u8g2.drawTriangle(x, y - 12, x + 8, y + 9, x - 8, y + 9);
    u8g2.setDrawColor(0);
    u8g2.drawTriangle(x, y + 1, x + 4, y + 8, x - 4, y + 8);
    u8g2.setDrawColor(1);
    u8g2.drawLine(x, y - 12, x + 8, y + 9);
    u8g2.drawLine(x, y - 12, x - 8, y + 9);
    u8g2.drawLine(x - 8, y + 9, x + 8, y + 9);
  }

  void drawArc(int16_t cx, int16_t cy, int16_t radius, int8_t turnSide) {
    if (turnSide == 0) return;
    const int16_t start = turnSide > 0 ? 180 : 0;
    const int16_t end = turnSide > 0 ? 315 : -135;
    for (int16_t r = radius - 1; r <= radius + 1; ++r) {
      int16_t priorX = 0;
      int16_t priorY = 0;
      bool hasPrior = false;
      const int16_t step = turnSide > 0 ? 6 : -6;
      for (int16_t deg = start; turnSide > 0 ? deg <= end : deg >= end; deg += step) {
        const float rad = deg * DEG_TO_RAD;
        const int16_t x = static_cast<int16_t>(roundf(cx + cosf(rad) * r));
        const int16_t y = static_cast<int16_t>(roundf(cy + sinf(rad) * r));
        if (hasPrior) u8g2.drawLine(priorX, priorY, x, y);
        priorX = x;
        priorY = y;
        hasPrior = true;
      }
    }
  }

  void drawTargetScale(int16_t cx, int16_t cy) {
    const int16_t left = cx - 28;
    const int16_t right = cx + 28;
    const int16_t top = cy - 11;
    const int16_t bottom = cy + 11;
    const int16_t innerTop = cy - 4;
    const int16_t innerBottom = cy + 4;

    u8g2.setDrawColor(0);
    u8g2.drawBox(left, innerTop, 56, 8);
    u8g2.drawBox(cx - 6, top, 12, 22);
    u8g2.setDrawColor(1);

    u8g2.drawLine(left, innerTop, cx - 6, innerTop);
    u8g2.drawLine(cx - 6, innerTop, cx - 6, top);
    u8g2.drawLine(cx - 6, top, cx + 6, top);
    u8g2.drawLine(cx + 6, top, cx + 6, innerTop);
    u8g2.drawLine(cx + 6, innerTop, right, innerTop);
    u8g2.drawLine(right, innerTop, right, innerBottom);
    u8g2.drawLine(right, innerBottom, cx + 6, innerBottom);
    u8g2.drawLine(cx + 6, innerBottom, cx + 6, bottom);
    u8g2.drawLine(cx + 6, bottom, cx - 6, bottom);
    u8g2.drawLine(cx - 6, bottom, cx - 6, innerBottom);
    u8g2.drawLine(cx - 6, innerBottom, left, innerBottom);
    u8g2.drawLine(left, innerBottom, left, innerTop);
  }

  void drawAltitudeField() {
    const uint8_t altType = settings.disp_thmPageAltType == altType_MSL ? altType_MSL : altType_GPS;
    u8g2.setFont(leaf_labels);
    u8g2.setCursor(ALT_LABEL_X, ALT_BASELINE_Y - 8);
    u8g2.print(settings.units_alt ? "ft" : "m");
    u8g2.setCursor(ALT_LABEL_X, ALT_BASELINE_Y);
    print_alt_label(altType);

    display_alt_type(ALT_X, ALT_BASELINE_Y, leaf_21h, altType);

    if (thermalCorePageCursor == cursor_thermalCorePage_alt) {
      display_selectionBox(ALT_LABEL_X - 1, ALT_BASELINE_Y - 23, 96 - (ALT_LABEL_X - 1), 25, 6);
    }
  }

  void drawClimbRateField(int32_t climbRate) {
    u8g2.setFont(leaf_8x14);
    u8g2.setCursor(CLIMB_X, CLIMB_BASELINE_Y);
    if (climbRate >= 0) {
      u8g2.print('+');
    } else {
      u8g2.print('-');
      climbRate *= -1;
    }

    if (settings.units_climb) {
      climbRate = climbRate * 197 / 1000 * 10;
      if (climbRate < 1000) u8g2.print(" ");
      if (climbRate < 100) u8g2.print(" ");
      if (climbRate < 10) u8g2.print(" ");
      u8g2.print(climbRate);
    } else {
      climbRate = (climbRate + 5) / 10;
      const float climbInMS = static_cast<float>(climbRate) / 10;
      if (climbInMS < 10) u8g2.print(" ");
      u8g2.print(climbInMS, 1);
    }

    u8g2.setFont(leaf_5h);
    u8g2.print(" ");
    u8g2.setFont(leaf_8x14);
    u8g2.print(settings.units_climb ? 'f' : 'm');
  }

  void drawPlusGlyph(int16_t cx, int16_t cy) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(cx - 4, cy - 10, 8, 20);
    u8g2.drawBox(cx - 10, cy - 4, 20, 8);
    u8g2.setDrawColor(1);
    u8g2.drawBox(cx - 2, cy - 8, 4, 16);
    u8g2.drawBox(cx - 8, cy - 2, 16, 4);
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
    const ThermalCoreEstimate& estimate = thermalCore.estimate();
    const int8_t direction = estimate.direction;
    const int8_t turnSide = direction < 0 ? -1 : direction > 0 ? 1 : 0;
    const int16_t aircraftX = direction < 0   ? RIGHT_AIRCRAFT_X
                              : direction > 0 ? LEFT_AIRCRAFT_X
                                              : CENTER_AIRCRAFT_X;
    const int16_t adviceShift = estimate.valid ? (estimate.adviceQ7 * TARGET_SHIFT_MAX) / 127 : 0;
    const int16_t dynamicRadius = min<int16_t>(46, max<int16_t>(20, TARGET_RADIUS + adviceShift));
    const int16_t targetCx = turnSide == 0 ? aircraftX : aircraftX + turnSide * dynamicRadius;
    const bool hasTurn = turnSide != 0;
    const bool hasGuidance = estimate.valid && abs(estimate.adviceQ7) >= 10;

    display_varioBar(VARIO_BAR_TOP, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_WIDTH,
                     climbRate);
    drawAltitudeField();
    drawClimbRateField(climbRate);

    if (hasGuidance) {
      drawArc(targetCx, AIRCRAFT_Y, dynamicRadius, turnSide);
    }

    for (uint8_t i = 0; i < estimate.markerCount; ++i) {
      drawMarker(estimate.markers[i]);
    }

    if (hasTurn) {
      const int16_t neutralTargetCx = aircraftX + turnSide * TARGET_RADIUS;
      drawTargetScale(neutralTargetCx, AIRCRAFT_Y);
    }

    if (hasGuidance) drawPlusGlyph(targetCx, AIRCRAFT_Y);
    drawAircraft(aircraftX, AIRCRAFT_Y);
    u8g2.drawFrame(MAP_LEFT, MAP_TOP, MAP_SIZE, MAP_SIZE);
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
