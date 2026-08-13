#include "ui/display/pages/primary/page_thermal_track.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include "hardware/buttons.h"
#include "instruments/baro.h"
#include "logging/log.h"
#include "navigation/thermal_tracker.h"
#include "power.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/utils.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"

namespace {
  constexpr int16_t MAP_CX = 48;
  constexpr int16_t MAP_CY = 124;
  constexpr uint8_t MAP_D_500M = 50;
  constexpr uint8_t MAP_D_1500M = 74;
  constexpr uint8_t MAP_D_5000M = 96;
  constexpr uint8_t MAP_R_5000M = MAP_D_5000M / 2;
  constexpr uint8_t VARIO_BAR_TOP = 16;
  constexpr uint8_t VARIO_BAR_WIDTH = 17;
  constexpr uint8_t VARIO_BAR_HALF_HEIGHT = 50;
  constexpr uint8_t ALT_LABEL_X = VARIO_BAR_WIDTH + 2;
  constexpr uint8_t ALT_X = 28;
  constexpr uint8_t ALT_BASELINE_Y = 39;

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
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(27, 57);
    if (selected == nullptr) {
      u8g2.print(settings.units_climb ? "----fpm" : "--.-m/s");
      return;
    }
    u8g2.print(formatClimbRate(selected->avgClimbCms, settings.units_climb, true));
  }

  void printSelectedDistance(const ThermalDisplayItem* selected) {
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(34, 72);
    if (selected == nullptr) {
      u8g2.print(settings.units_distance ? "--.-mi" : "--.-km");
      return;
    }

    if (settings.units_distance) {
      constexpr float FEET_PER_METER = 3.28084f;
      constexpr float FEET_PER_MILE = 5280.0f;
      const float distanceFeet = selected->distanceM * FEET_PER_METER;
      if (distanceFeet <= 1000.0f) {
        u8g2.print(static_cast<uint16_t>(roundf(distanceFeet)));
        u8g2.print("ft");
      } else {
        u8g2.print(distanceFeet / FEET_PER_MILE, 1);
        u8g2.print("mi");
      }
    } else if (selected->distanceM < 1000) {
      u8g2.print(selected->distanceM);
      u8g2.print('m');
    } else {
      u8g2.print(selected->distanceM / 1000.0f, 1);
      u8g2.print("km");
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
    const int16_t y0 = MAP_CY - 6;

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
        drawClippedCircle(x, y, 6);
        break;
      }
    }
  }

  void drawAltitudeField() {
    const uint8_t altType = settings.disp_thmPageAltType == altType_MSL ? altType_MSL : altType_GPS;
    u8g2.setFont(leaf_labels);
    u8g2.setCursor(ALT_LABEL_X, ALT_BASELINE_Y - 8);
    u8g2.print(settings.units_alt ? "ft" : "m");
    u8g2.setCursor(ALT_LABEL_X, ALT_BASELINE_Y);
    print_alt_label(altType);

    display_alt_type(ALT_X, ALT_BASELINE_Y, leaf_21h, altType);

    if (thermalTrackPageCursor == cursor_thermalTrackPage_alt) {
      display_selectionBox(ALT_LABEL_X - 1, ALT_BASELINE_Y - 23, 96 - (ALT_LABEL_X - 1), 25, 6);
    }
  }

  void drawSelectedThermalReadout(const ThermalDisplayItem* selected) {
    printSelectedClimb(selected);
    printSelectedDistance(selected);
  }

  void toggleAltitudeType() {
    if (settings.disp_thmPageAltType == altType_MSL)
      settings.disp_thmPageAltType = altType_GPS;
    else
      settings.disp_thmPageAltType = altType_MSL;
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

    display_varioBar(VARIO_BAR_TOP, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_WIDTH,
                     climbRate);
    maskMapCircle();
    drawThermals();
    drawMapRingsAndLabels();
    drawUserTriangle();
    drawAltitudeField();
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
