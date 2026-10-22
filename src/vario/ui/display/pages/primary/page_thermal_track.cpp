#include "ui/display/pages/primary/page_thermal_track.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include "hardware/buttons.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logging/log.h"
#include "navigation/thermal_tracker.h"
#include "power.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/thermal_page_fields.h"
#include "ui/display/utils.h"
#include "ui/settings/settings.h"

namespace {
  constexpr int16_t MAP_CX = 48;
  constexpr int16_t MAP_CY = 124;
  constexpr uint8_t MAP_D_500M = 50;
  constexpr uint8_t MAP_D_1500M = 74;
  constexpr uint8_t MAP_D_5000M = 96;
  constexpr uint8_t MAP_R_5000M = MAP_D_5000M / 2;
  constexpr uint8_t ALT_X = 15;
  constexpr uint8_t ALT_VALUE_BASELINE_Y = 30;
  constexpr uint8_t ALT_TYPE_BASELINE_Y = 33;
  constexpr uint8_t WIND_X = 77;
  constexpr uint8_t WIND_Y = 33;
  constexpr uint8_t SELECTED_RIGHT = 96;  // exclusive: last screen pixel is x=95
  constexpr uint8_t SELECTED_TOP = 57;
  constexpr uint8_t SELECTED_BOTTOM = 74;
  constexpr uint8_t SELECTED_BASELINE_Y = 71;
  // Fixed slots leave a blank column between the arrows, distance and strength.
  constexpr uint8_t SELECTED_STRENGTH_LEFT = 62;
  constexpr uint8_t SELECTED_DISTANCE_LEFT = 23;
  constexpr uint8_t SELECTED_DISTANCE_RIGHT = 53;
  constexpr uint8_t DISTANCE_LEFT_ARROW_X = 15;
  constexpr uint8_t DISTANCE_RIGHT_ARROW_X = 54;
  constexpr uint8_t SELECTED_THERMAL_RADIUS = 6;

  enum ThermalTrackPageItem : uint8_t {
    cursor_thermalTrackPage_none,
    cursor_thermalTrackPage_alt,
    cursor_thermalTrackPage_timer,
  };
  constexpr uint8_t THERMAL_TRACK_CURSOR_MAX = cursor_thermalTrackPage_timer;
  constexpr uint8_t THERMAL_TRACK_CURSOR_TIMEOUT = 8;
  int8_t thermalTrackPageCursor = cursor_thermalTrackPage_none;
  uint8_t thermalTrackPageCursorTimeCount = 0;

  constexpr uint8_t POINTER_W = 12;
  constexpr uint8_t POINTER_H = 11;
  constexpr int16_t POINTER_TOP = MAP_CY - 2;
  constexpr int16_t POINTER_TIP_X = MAP_CX;
  constexpr int16_t POINTER_TIP_Y = POINTER_TOP + 1;
  constexpr uint8_t POINTER_TRANSPARENT = 2;
  constexpr uint8_t POINTER_WHITE = 0;
  constexpr uint8_t POINTER_BLACK = 1;
  constexpr uint8_t POINTER_GLYPH[POINTER_H][POINTER_W] = {
      {2, 2, 2, 2, 2, 0, 0, 2, 2, 2, 2, 2}, {2, 2, 2, 2, 0, 1, 1, 0, 2, 2, 2, 2},
      {2, 2, 2, 0, 1, 0, 0, 1, 0, 2, 2, 2}, {2, 2, 2, 0, 1, 0, 0, 1, 0, 2, 2, 2},
      {2, 2, 0, 1, 0, 2, 2, 0, 1, 0, 2, 2}, {2, 2, 0, 1, 0, 2, 2, 0, 1, 0, 2, 2},
      {2, 0, 1, 0, 2, 2, 2, 2, 0, 1, 0, 2}, {2, 0, 1, 0, 2, 2, 2, 2, 0, 1, 0, 2},
      {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0}, {2, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 2},
      {2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2},
  };

  void printSelectedClimb(const ThermalDisplayItem* selected) {
    char value[14];
    const char unit = static_cast<char>(131 + settings.units_climb);
    if (selected == nullptr) {
      snprintf(value, sizeof(value), "%s%c", settings.units_climb ? "----" : "--.-", unit);
    } else if (settings.units_climb) {
      const int32_t fpm = static_cast<int32_t>(selected->avgClimbCms) * 197 / 1000 * 10;
      snprintf(value, sizeof(value), "%ld%c", static_cast<long>(fpm), unit);
    } else {
      snprintf(value, sizeof(value), "%.1f%c", selected->avgClimbCms / 100.0f, unit);
    }
    u8g2.setFont(leaf_6x12);
    const int16_t x = SELECTED_RIGHT - u8g2.getStrWidth(value);
    u8g2.setClipWindow(SELECTED_STRENGTH_LEFT, SELECTED_TOP, SELECTED_RIGHT, SELECTED_BOTTOM);
    u8g2.drawStr(x, SELECTED_BASELINE_Y, value);
    u8g2.setMaxClipWindow();
  }

  void printSelectedDistance(const ThermalDisplayItem* selected) {
    char value[14];
    const char feet[] = {static_cast<char>(128), '\0'};
    const char* unit = settings.units_distance ? "mi" : "km";
    const uint8_t* unitFont = leaf_5x8;
    if (selected == nullptr) {
      snprintf(value, sizeof(value), "--");
    } else {
      const float shortDistance =
          max<int16_t>(0, selected->distanceM) * (settings.units_distance ? 3.28084f : 1.0f);
      if (shortDistance < 1000.0f) {
        unit = settings.units_distance ? feet : "m";
        if (settings.units_distance) unitFont = leaf_6x12;
        snprintf(value, sizeof(value), "%u",
                 static_cast<unsigned>(min<float>(999, roundf(shortDistance))));
      } else {
        const float longDistance = shortDistance / (settings.units_distance ? 5280.0f : 1000.0f);
        snprintf(value, sizeof(value), longDistance < 10 ? "%.1f" : "%.0f",
                 min<float>(longDistance < 10 ? 9.9f : 99.0f, longDistance));
      }
    }
    u8g2.setFont(leaf_6x12);
    const int16_t valueWidth = u8g2.getStrWidth(value);
    u8g2.setFont(unitFont);
    const int16_t width = valueWidth + 1 + u8g2.getStrWidth(unit);
    const int16_t x =
        SELECTED_DISTANCE_LEFT + (SELECTED_DISTANCE_RIGHT - SELECTED_DISTANCE_LEFT - width) / 2;
    u8g2.setClipWindow(SELECTED_DISTANCE_LEFT, SELECTED_TOP, SELECTED_DISTANCE_RIGHT,
                       SELECTED_BOTTOM);
    u8g2.setFont(leaf_6x12);
    u8g2.drawStr(x, SELECTED_BASELINE_Y, value);
    u8g2.setFont(unitFont);
    u8g2.drawStr(x + valueWidth + 1, SELECTED_BASELINE_Y, unit);
    u8g2.setMaxClipWindow();
    if (selected != nullptr) {
      display_turnIndicators(DISTANCE_LEFT_ARROW_X, DISTANCE_RIGHT_ARROW_X, SELECTED_BASELINE_Y,
                             selected->turnDeg);
    }
  }

  bool mapContainsPixel(int16_t x, int16_t y) {
    const int32_t dx2 = int32_t(x) * 2 + 1 - 2 * MAP_CX;
    const int32_t dy2 = int32_t(y) * 2 + 1 - 2 * MAP_CY;
    return dx2 * dx2 + dy2 * dy2 <= int32_t(MAP_D_5000M) * MAP_D_5000M;
  }

  bool softDiscContainsPixel(int16_t px, int16_t py, int16_t cx, int16_t cy, uint8_t radius) {
    const int16_t dx = px - cx;
    const int16_t dy = py - cy;
    const int32_t diameterPlusOne = 2 * radius + 1;
    return 4 * (int32_t(dx) * dx + int32_t(dy) * dy) <= diameterPlusOne * diameterPlusOne;
  }

  bool softCircleContainsPixel(int16_t px, int16_t py, int16_t cx, int16_t cy, uint8_t radius) {
    const int16_t dx = px - cx;
    const int16_t dy = py - cy;
    const int32_t distance4 = 4 * (int32_t(dx) * dx + int32_t(dy) * dy);
    const int32_t outer = int32_t(2 * radius + 1) * (2 * radius + 1);
    const int32_t inner = radius > 0 ? int32_t(2 * radius - 1) * (2 * radius - 1) : 0;
    return distance4 <= outer && distance4 >= inner;
  }

  void drawClippedPixel(int16_t x, int16_t y) {
    if (mapContainsPixel(x, y)) u8g2.drawPixel(x, y);
  }

  void drawClippedDisc(int16_t cx, int16_t cy, uint8_t radius) {
    for (int16_t y = cy - radius; y <= cy + radius; y++) {
      int16_t segmentStart = INT16_MIN;
      for (int16_t x = cx - radius; x <= cx + radius; x++) {
        const bool draw = softDiscContainsPixel(x, y, cx, cy, radius) && mapContainsPixel(x, y);
        if (draw && segmentStart == INT16_MIN) {
          segmentStart = x;
        } else if (!draw && segmentStart != INT16_MIN) {
          u8g2.drawHLine(segmentStart, y, x - segmentStart);
          segmentStart = INT16_MIN;
        }
      }
      if (segmentStart != INT16_MIN)
        u8g2.drawHLine(segmentStart, y, cx + radius - segmentStart + 1);
    }
  }

  void drawClippedCircle(int16_t cx, int16_t cy, uint8_t radius) {
    for (int16_t y = cy - radius; y <= cy + radius; y++) {
      for (int16_t x = cx - radius; x <= cx + radius; x++) {
        if (softCircleContainsPixel(x, y, cx, cy, radius)) drawClippedPixel(x, y);
      }
    }
  }

  void drawSelectedThermalLine(const ThermalDisplayItem* selected) {
    if (selected == nullptr || !selected->valid) return;
    const int16_t targetX = MAP_CX + selected->xOffset;
    const int16_t targetY = MAP_CY + selected->yOffset;
    int16_t x = POINTER_TIP_X;
    int16_t y = POINTER_TIP_Y;
    const int16_t dx = abs(targetX - x);
    const int16_t dy = -abs(targetY - y);
    const int16_t stepX = x < targetX ? 1 : -1;
    const int16_t stepY = y < targetY ? 1 : -1;
    int16_t error = dx + dy;
    while (true) {
      // Stop at the selection ring's first edge; never draw into the hollow marker.
      if (softDiscContainsPixel(x, y, targetX, targetY, SELECTED_THERMAL_RADIUS)) {
        if (softCircleContainsPixel(x, y, targetX, targetY, SELECTED_THERMAL_RADIUS)) {
          drawClippedPixel(x, y);
        }
        break;
      }
      drawClippedPixel(x, y);
      const int16_t twiceError = 2 * error;
      if (twiceError >= dy) {
        error += dy;
        x += stepX;
      }
      if (twiceError <= dx) {
        error += dx;
        y += stepY;
      }
    }
  }

  void drawQualityMarker(int16_t x, int16_t y, uint8_t quality) {
    quality = constrain(quality, 1, 5);
    if (quality >= 4) {
      drawClippedDisc(x, y, 4);
      if (quality == 4 && mapContainsPixel(x, y)) {
        u8g2.setDrawColor(0);
        u8g2.drawPixel(x, y);
        u8g2.setDrawColor(1);
      }
      return;
    }

    drawClippedCircle(x, y, 4);
    if (quality >= 2) drawClippedCircle(x, y, 3);
    if (quality >= 3) drawClippedCircle(x, y, 2);
  }

  void drawMapRingsAndLabels() {
    drawCircleD(MAP_CX, MAP_CY, MAP_D_500M);
    drawCircleD(MAP_CX, MAP_CY, MAP_D_1500M);
    drawCircleD(MAP_CX, MAP_CY, MAP_D_5000M);
  }

  void maskMapCircle() {
    u8g2.setDrawColor(0);
    u8g2.drawDisc(MAP_CX, MAP_CY, MAP_R_5000M);
    u8g2.setDrawColor(1);
  }

  void drawDistanceScaleLabels() {
    u8g2.setFont(leaf_5x8);
    constexpr uint8_t labelHeight = 8;
    constexpr uint8_t padding = 1;

    const auto drawLabel = [&](int16_t x, int16_t baselineY, const char* text,
                               uint8_t horizontalPadding) {
      const uint8_t labelWidth = u8g2.getStrWidth(text);
      u8g2.setDrawColor(0);
      u8g2.drawRBox(x - horizontalPadding, baselineY - labelHeight - 1,
                    labelWidth + 2 * horizontalPadding, labelHeight + 2 * padding, 1);
      u8g2.setDrawColor(1);
      u8g2.setCursor(x, baselineY);
      u8g2.print(text);
    };

    drawLabel(41, 151, settings.units_distance ? "0.3" : "0.5", padding);
    drawLabel(41, 163, settings.units_distance ? "1.0" : "1.5", 0);
    drawLabel(settings.units_distance ? 41 : 40, 175, settings.units_distance ? "3mi" : "5km",
              padding);
    u8g2.setDrawColor(1);
  }

  void drawUserTriangle() {
    const int16_t x0 = MAP_CX - POINTER_W / 2;
    const int16_t y0 = POINTER_TOP;

    for (uint8_t y = 0; y < POINTER_H; ++y) {
      for (uint8_t x = 0; x < POINTER_W; ++x) {
        const uint8_t pixel = POINTER_GLYPH[y][x];
        if (pixel == POINTER_TRANSPARENT) continue;
        u8g2.setDrawColor(pixel == POINTER_BLACK ? 1 : 0);
        u8g2.drawPixel(x0 + x, y0 + y);
      }
    }
    u8g2.setDrawColor(1);
  }

  void drawThermals() {
    const ThermalDisplayItem* items = thermalTracker.displayItems();
    const uint8_t count = thermalTracker.displayItemCount();
    for (uint8_t i = 0; i < count; ++i) {
      if (!items[i].valid) continue;
      const int16_t x = MAP_CX + items[i].xOffset;
      const int16_t y = MAP_CY + items[i].yOffset;
      drawQualityMarker(x, y, items[i].quality);
    }
    for (uint8_t i = 0; i < count; ++i) {
      if (items[i].valid && items[i].selected) {
        const int16_t x = MAP_CX + items[i].xOffset;
        const int16_t y = MAP_CY + items[i].yOffset;
        drawClippedCircle(x, y, SELECTED_THERMAL_RADIUS);
        break;
      }
    }
  }

  void drawAltitudeField() {
    const uint8_t altType =
        settings.disp_userPageAltType == altType_MSL ? altType_MSL : altType_GPS;
    if (altType == altType_MSL && baro.state() != Barometer::State::Ready) {
      u8g2.setFont(leaf_10x17);
      u8g2.setCursor(ALT_X, ALT_VALUE_BASELINE_Y);
      u8g2.print(" ---");
      return;
    }
    const int32_t altitudeCm = altType == altType_MSL
                                   ? baro.altAdjusted()
                                   : static_cast<int32_t>(100 * gps.altitude.meters());
    const int32_t altitude = settings.units_alt ? altitudeCm * 100 / 3048 : altitudeCm / 100;
    uint8_t tensX;
    const uint8_t endX =
        thermal_page::drawCoreAltitude(altitude, ALT_X, ALT_VALUE_BASELINE_Y, &tensX);
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(tensX, ALT_TYPE_BASELINE_Y);
    u8g2.print(altType == altType_MSL ? "MSL" : "GPS");

    if (thermalTrackPageCursor == cursor_thermalTrackPage_alt) {
      // Preserve the existing right edge even though the compact value is now narrower.
      const bool compact = abs(altitude) >= 10000 || altitude <= -1000;
      const uint8_t cursorRight = endX + (compact ? 3 : 0);
      display_selectionBox(ALT_X, ALT_VALUE_BASELINE_Y - 18, cursorRight - ALT_X + 1, 22, 7);
    }
  }

  void drawSelectedThermalReadout(const ThermalDisplayItem* selected) {
    printSelectedClimb(selected);
    printSelectedDistance(selected);
  }

  void toggleAltitudeType() {
    if (settings.disp_userPageAltType == altType_MSL)
      settings.disp_userPageAltType = altType_GPS;
    else
      settings.disp_userPageAltType = altType_MSL;
    speaker.playSound(fx::neutral);
  }

  bool thermalTrackPageVolumeShortcut(Button button, ButtonEvent state) {
    if (!settings.volumeShortcut || (button != Button::UP && button != Button::DOWN) ||
        state != ButtonEvent::INCREMENTED) {
      return false;
    }

    if (!settings.adjustShortcutVolume(button)) buttons.consumeButton();
    return true;
  }

  void thermalTrackPageCursorMove(Button button) {
    if (button == Button::UP) {
      thermalTrackPageCursor--;
      if (thermalTrackPageCursor < 0) thermalTrackPageCursor = THERMAL_TRACK_CURSOR_MAX;
    }
    if (button == Button::DOWN) {
      thermalTrackPageCursor++;
      if (thermalTrackPageCursor > THERMAL_TRACK_CURSOR_MAX) thermalTrackPageCursor = 0;
    }
    speaker.playSound(thermalTrackPageCursor == cursor_thermalTrackPage_none ? fx::doubleClick
                                                                             : fx::click);
  }
}  // namespace

void thermalTrackPage_reset() {
  thermalTrackPageCursor = cursor_thermalTrackPage_none;
  thermalTrackPageCursorTimeCount = 0;
}

void thermalTrackPage_draw() {
  if (thermalTrackPageCursor != cursor_thermalTrackPage_none &&
      thermalTrackPageCursorTimeCount++ >= THERMAL_TRACK_CURSOR_TIMEOUT) {
    thermalTrackPageCursor = cursor_thermalTrackPage_none;
    thermalTrackPageCursorTimeCount = 0;
  }

  u8g2.firstPage();
  do {
    display_headerAndFooter(thermalTrackPageCursor == cursor_thermalTrackPage_timer, false);

    const ThermalDisplayItem* selected = thermalTracker.selectedDisplayItem();
    const int32_t climbRate = baro.climbRateFilteredValid() ? baro.climbRateFiltered() : 0;
    const int32_t displayClimbRate = baro.climbRateFilteredValid() ? baro.climbRateForDisplay() : 0;

    thermal_page::drawVario(climbRate, 2, thermal_page::CORE_VARIO_BAR_WIDTH);
    maskMapCircle();
    drawThermals();
    drawMapRingsAndLabels();
    drawUserTriangle();
    drawSelectedThermalLine(selected);
    display_windSockRing(WIND_X, WIND_Y, 12, 7, true);
    drawAltitudeField();
    thermal_page::drawCoreClimbRate(displayClimbRate);
    drawSelectedThermalReadout(selected);
    drawDistanceScaleLabels();
  } while (u8g2.nextPage());
}

void thermalTrackPage_button(Button button, ButtonEvent state, uint8_t count) {
  thermalTrackPageCursorTimeCount = 0;

  switch (thermalTrackPageCursor) {
    case cursor_thermalTrackPage_none:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (thermalTrackPageVolumeShortcut(button, state)) {
            break;
          } else if (state == ButtonEvent::CLICKED) {
            thermalTrackPageCursorMove(button);
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
    case cursor_thermalTrackPage_alt:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) thermalTrackPageCursorMove(button);
          break;
        case Button::LEFT:
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED) toggleAltitudeType();
          break;
      }
      break;
    case cursor_thermalTrackPage_timer:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) thermalTrackPageCursorMove(button);
          break;
        case Button::LEFT:
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED && !flightTimer_isRunning()) {
            flightTimer_start();
            thermalTrackPageCursor = cursor_thermalTrackPage_none;
          } else if (state == ButtonEvent::HELD && flightTimer_isRunning()) {
            buttons.consumeButton();
            flightTimer_stop();
            thermalTrackPageCursor = cursor_thermalTrackPage_none;
          }
          break;
      }
      break;
  }
  display.update();
}
