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

// The Thermal Core page.  The top half answers "is this thermal worth staying in" with the
// average climb over the last half minute; the map below answers "where should my circle be".
//
// The map is track-up and drawn in the AIR MASS: everything ThermalCore hands over has already
// had the wind taken out of it, so a well-centred circle draws as a circle and not as the
// downwind spiral the ground track really is.  The glider is pushed to the outside of its own
// turn so the whole circle fits, and the scale follows the circle the pilot is actually flying,
// which means a 20 m circle and a 45 m circle fill the window the same way.

namespace {
  // The numbers get 48 px and the map gets the rest.  The map is where the answer is, and the
  // extra 12 px it gains over a square window is exactly what lets the status line sit below the
  // circle instead of being erased out of the middle of it.
  constexpr int16_t MAP_LEFT = 0;
  constexpr int16_t MAP_TOP = 64;
  constexpr int16_t MAP_W = 96;
  constexpr int16_t MAP_H = 108;
  constexpr int16_t MAP_RIGHT = MAP_LEFT + MAP_W - 1;
  constexpr int16_t MAP_BOTTOM = MAP_TOP + MAP_H - 1;

  constexpr int16_t STATUS_H = 17;
  constexpr int16_t STATUS_TOP = MAP_BOTTOM - STATUS_H;
  constexpr int16_t GLIDER_Y = (MAP_TOP + STATUS_TOP) / 2;
  constexpr int16_t GLIDER_CENTER_X = MAP_LEFT + MAP_W / 2;
  // Pushed this far to the outside of the turn.  With the circle drawn at TURN_RADIUS_PX the far
  // side then lands just inside the opposite edge, which is the most of the circle that fits.
  constexpr int16_t TURN_BIAS_PX = 20;
  constexpr int16_t TURN_RADIUS_PX = 32;

  constexpr uint8_t VARIO_BAR_TOP = 16;
  constexpr uint8_t VARIO_BAR_WIDTH = 17;
  constexpr uint8_t VARIO_BAR_HALF_HEIGHT = 23;

  // Two rows, each built the same way: what it is on the left, the number, its unit on the right.
  // One unit per row and nothing else, because the header already puts a KPH directly above this
  // block and a second stack of units beside a signed number is what made a climb rate read as a
  // negative ground speed.
  constexpr uint8_t FIELD_LEFT = VARIO_BAR_WIDTH + 2;
  constexpr uint8_t FIELD_RIGHT = 95;
  constexpr uint8_t AVG_BASELINE_Y = 35;
  constexpr uint8_t ALT_BASELINE_Y = 57;

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

  // ============================ map primitives ============================
  //
  // Everything below draws through clipped helpers rather than calling u8g2 directly, because
  // u8g2_uint_t is UNSIGNED: hand drawLine or drawDisc a negative x and it wraps to about 65535,
  // and the primitive then paints a line clear across the display.  The core marker reaches
  // 15 px out from its centre, so as soon as the core sits near an edge the marker is partly off
  // the map by design -- it has to clip, not wrap.

  // The status line owns the bottom of the window, so nothing on the map may be drawn into it.
  constexpr int16_t MAP_CLIP_BOTTOM = STATUS_TOP - 1;

  bool insideMap(int16_t x, int16_t y, int16_t margin) {
    return x - margin >= MAP_LEFT && x + margin <= MAP_RIGHT && y - margin >= MAP_TOP &&
           y + margin <= MAP_CLIP_BOTTOM;
  }

  void mapPixel(int16_t x, int16_t y) {
    if (x < MAP_LEFT || x > MAP_RIGHT || y < MAP_TOP || y > MAP_CLIP_BOTTOM) return;
    u8g2.drawPixel(x, y);
  }

  void mapHLine(int16_t x, int16_t y, int16_t w) {
    if (w <= 0 || y < MAP_TOP || y > MAP_CLIP_BOTTOM) return;
    int16_t first = x < MAP_LEFT ? MAP_LEFT : x;
    const int16_t last = x + w - 1 > MAP_RIGHT ? MAP_RIGHT : x + w - 1;
    if (last < first) return;
    u8g2.drawHLine(first, y, last - first + 1);
  }

  void mapVLine(int16_t x, int16_t y, int16_t h) {
    if (h <= 0 || x < MAP_LEFT || x > MAP_RIGHT) return;
    int16_t first = y < MAP_TOP ? MAP_TOP : y;
    const int16_t last = y + h - 1 > MAP_CLIP_BOTTOM ? MAP_CLIP_BOTTOM : y + h - 1;
    if (last < first) return;
    u8g2.drawVLine(x, first, last - first + 1);
  }

  void mapBox(int16_t x, int16_t y, int16_t w, int16_t h) {
    for (int16_t row = 0; row < h; row++) mapHLine(x, y + row, w);
  }

  // Discs and rings as horizontal spans, so the clipping is the same clipping as everything else.
  void mapDisc(int16_t cx, int16_t cy, int16_t r) {
    for (int16_t dy = -r; dy <= r; dy++) {
      const int16_t half = static_cast<int16_t>(sqrtf(static_cast<float>(r * r - dy * dy)) + 0.5f);
      mapHLine(cx - half, cy + dy, 2 * half + 1);
    }
  }

  void mapRing(int16_t cx, int16_t cy, int16_t r) {
    for (int16_t d = -r; d <= r; d++) {
      const int16_t off = static_cast<int16_t>(sqrtf(static_cast<float>(r * r - d * d)) + 0.5f);
      mapPixel(cx - off, cy + d);
      mapPixel(cx + off, cy + d);
      mapPixel(cx + d, cy - off);
      mapPixel(cx + d, cy + off);
    }
  }

  void stampRing5(int16_t x, int16_t y) {
    mapHLine(x - 1, y - 2, 3);
    mapHLine(x - 1, y + 2, 3);
    mapVLine(x - 2, y - 1, 3);
    mapVLine(x + 2, y - 1, 3);
  }

  void stampRing7(int16_t x, int16_t y) {
    mapHLine(x - 1, y - 3, 3);
    mapHLine(x - 1, y + 3, 3);
    mapVLine(x - 3, y - 1, 3);
    mapVLine(x + 3, y - 1, 3);
    mapPixel(x - 2, y - 2);
    mapPixel(x + 2, y - 2);
    mapPixel(x - 2, y + 2);
    mapPixel(x + 2, y + 2);
  }

  // Lift is drawn as area, which is the one visual channel a one-bit display has left once
  // position is spoken for.  While circling the bands are relative to the lap's own average, so
  // the trail shows the shape of the thermal rather than going uniformly large inside a strong
  // one -- an even necklace of rings means centred, a fat side means go that way.
  //
  // Trail points are dropped whole rather than clipped: half a ring at the edge of the map reads
  // as a different lift band, which is worse than not drawing it.
  void drawTrailPoint(int16_t x, int16_t y, ThermalCoreLift lift) {
    switch (lift) {
      case ThermalCoreLift::Sink:
        mapPixel(x, y);
        break;
      case ThermalCoreLift::Weak:
        if (!insideMap(x, y, 1)) return;
        mapVLine(x, y - 1, 3);
        mapHLine(x - 1, y, 3);
        break;
      case ThermalCoreLift::Even:
        if (!insideMap(x, y, 2)) return;
        stampRing5(x, y);
        break;
      case ThermalCoreLift::Strong:
        if (!insideMap(x, y, 2)) return;
        stampRing5(x, y);
        mapPixel(x, y);
        break;
      case ThermalCoreLift::Best:
        if (!insideMap(x, y, 3)) return;
        stampRing7(x, y);
        mapBox(x - 1, y - 1, 3, 3);
        break;
    }
  }

  // The centre of the circle the pilot is flying right now.  On its own it says nothing; next to
  // the core marker it turns the correction into something with a visible size -- the gap between
  // the two marks is exactly how far the circle has to move.
  void drawCircleCentre(int16_t x, int16_t y) {
    if (!insideMap(x, y, 0)) return;
    mapHLine(x - 3, y, 3);
    mapHLine(x + 1, y, 3);
    mapVLine(x, y - 3, 3);
    mapVLine(x, y + 1, 3);
  }

  // The core: the one thing on this page worth finding at a glance, so it is drawn with more ink
  // than anything else on the map.  Every trail point is already a thin hollow ring, so the mark
  // that means something different is built out of what a ring is not -- a filled middle, a
  // double-struck rim, and four heavy spokes.  Its background is cleared first, spoke corridors
  // included, so it stays solid sitting on top of the trail.
  void drawCore(int16_t x, int16_t y) {
    if (!insideMap(x, y, 0)) return;

    constexpr int8_t ARM_DX[4] = {-1, 1, 0, 0};
    constexpr int8_t ARM_DY[4] = {0, 0, -1, 1};
    constexpr int16_t ARM_INNER = 10;
    constexpr int16_t ARM_OUTER = 15;

    // Each spoke is axis aligned, so it is a clipped H or V line rather than a general one.
    const auto spoke = [&](uint8_t arm, int8_t side, int16_t inner, int16_t outer) {
      const int16_t dx = ARM_DX[arm];
      const int16_t dy = ARM_DY[arm];
      const int16_t length = outer - inner + 1;
      if (dx != 0) {
        const int16_t start = dx < 0 ? x - outer : x + inner;
        mapHLine(start, y + side, length);
      } else {
        const int16_t start = dy < 0 ? y - outer : y + inner;
        mapVLine(x + side, start, length);
      }
    };

    u8g2.setDrawColor(0);
    mapDisc(x, y, 9);
    for (uint8_t arm = 0; arm < 4; arm++) {
      for (int8_t side = -2; side <= 2; side++) spoke(arm, side, ARM_INNER - 1, ARM_OUTER + 1);
    }
    u8g2.setDrawColor(1);

    mapBox(x - 2, y - 2, 5, 5);
    mapRing(x, y, 7);
    mapRing(x, y, 8);
    for (uint8_t arm = 0; arm < 4; arm++) {
      for (int8_t side = 0; side <= 1; side++) spoke(arm, side, ARM_INNER, ARM_OUTER);
    }
  }

  // Half the size of the arrow this page used to draw.  It has to orient the pilot, not compete
  // with the circle it is sitting inside.
  void drawGlider(int16_t x, int16_t y) { display_gliderPointer(x, y, 0.0f, 8); }

  // ============================ number fields ============================

  // Same formatting as display_climbRate, but written into a buffer so the caller can measure it
  // and right-align it.  Guessing a fixed-width font's advance from its name is how a field ends
  // up hanging off the edge of the screen or sitting on top of its own label.
  void formatClimb(int32_t climbCms, char* out, size_t size) {
    const char sign = climbCms >= 0 ? '+' : '-';
    if (climbCms < 0) climbCms = -climbCms;

    if (settings.units_climb) {
      snprintf(out, size, "%c%ld", sign, static_cast<long>(climbCms * 197 / 1000 * 10));
    } else {
      const int32_t tenths = (climbCms + 5) / 10;
      snprintf(out, size, "%c%ld.%ld", sign, static_cast<long>(tenths / 10),
               static_cast<long>(tenths % 10));
    }
  }

  // Draws text right-aligned so its last pixel lands on `right`, without letting it start before
  // `leftLimit`.
  void printRightAligned(const char* text, int16_t right, int16_t leftLimit, uint8_t baseline) {
    const int16_t width = u8g2.getStrWidth(text);
    int16_t x = right + 1 - width;
    if (x < leftLimit) x = leftLimit;
    u8g2.setCursor(x, baseline);
    u8g2.print(text);
  }

  // The average climb over the last half minute: what decides whether this thermal is worth
  // staying in.  Its unit is printed hard against the number rather than parked in a label
  // column, because the header's ground speed sits directly above this field -- with "m/s" over
  // on the left, a signed climb rate reads as a negative KPH.
  void drawAverageClimbField(const ThermalCoreState& core) {
    u8g2.setFont(leaf_labels);
    const char* unit = settings.units_climb ? "fpm" : "m/s";
    const int16_t unitWidth = u8g2.getStrWidth(unit);
    u8g2.setCursor(FIELD_RIGHT + 1 - unitWidth, AVG_BASELINE_Y);
    u8g2.print(unit);
    u8g2.setCursor(FIELD_LEFT, AVG_BASELINE_Y);
    u8g2.print("AVG");
    const int16_t labelRight = FIELD_LEFT + u8g2.getStrWidth("AVG");

    char text[12] = "---";
    if (core.avgClimbValid) formatClimb(core.avgClimbCms, text, sizeof(text));
    u8g2.setFont(leaf_10x17);
    printRightAligned(text, FIELD_RIGHT - unitWidth - 4, labelRight + 2, AVG_BASELINE_Y);
  }

  // The altitude the way display_alt writes it, but into a buffer.  The vario fonts carry narrow
  // punctuation to save space, so a thousands comma is not one glyph wide and the string cannot
  // be measured by counting characters -- which is the whole reason this is formatted here rather
  // than handed straight to display_alt_type.
  void formatAltitude(int32_t altCm, char* out, size_t size) {
    int32_t value = settings.units_alt ? altCm * 100 / 3048 : altCm / 100;
    if (value < -9999) {
      snprintf(out, size, "---");
      return;
    }
    if (value > 99999) value = 99999;

    const char* sign = value < 0 ? "-" : "";
    if (value < 0) value = -value;
    if (value > 999) {
      snprintf(out, size, "%s%ld,%03ld", sign, static_cast<long>(value / 1000),
               static_cast<long>(value % 1000));
    } else {
      snprintf(out, size, "%s%ld", sign, static_cast<long>(value));
    }
  }

  // Altitude, on the same left-label / number / right-unit rhythm as the row above it.  The
  // instantaneous climb used to sit here as a second small number; the vario bar running the full
  // height of this block already shows it, and so does the sound, so all it did was crowd the two
  // numbers that are not available any other way.
  void drawAltitudeRow() {
    const bool msl = settings.disp_thmPageAltType == altType_MSL;

    u8g2.setFont(leaf_labels);
    const char* unit = settings.units_alt ? "ft" : "m";
    const int16_t unitWidth = u8g2.getStrWidth(unit);
    u8g2.setCursor(FIELD_RIGHT + 1 - unitWidth, ALT_BASELINE_Y);
    u8g2.print(unit);
    u8g2.setCursor(FIELD_LEFT, ALT_BASELINE_Y);
    u8g2.print(msl ? "MSL" : "GPS");
    const int16_t labelRight = FIELD_LEFT + u8g2.getStrWidth("GPS");

    char text[12];
    formatAltitude(msl ? baro.altAdjusted() : static_cast<int32_t>(100 * gps.altitude.meters()),
                   text, sizeof(text));
    u8g2.setFont(leaf_7x13);
    printRightAligned(text, FIELD_RIGHT - unitWidth - 4, labelRight + 2, ALT_BASELINE_Y);

    if (thermalCorePageCursor == cursor_thermalCorePage_alt) {
      display_selectionBox(FIELD_LEFT - 2, ALT_BASELINE_Y - 12, 96 - (FIELD_LEFT - 2), 16, 6);
    }
  }

  // ============================ the map ============================

  // How far the circle has to move, which is the number a pilot acts on.
  void formatOffset(const ThermalCoreState& core, char* out, size_t size) {
    snprintf(out, size, "%dm", static_cast<int>(roundf(core.coreOffsetM)));
  }

  // The status line, in its own band below the circle.  There is one instruction on this page
  // worth reading in turbulence with the wing moving around, and it is OPEN: the few seconds each
  // lap when flying straight walks the circle onto the core.  So OPEN takes the whole band,
  // inverted, in the largest font that fits -- and the band is sized to the glyphs so no part of
  // the word falls outside the black it is knocked out of.
  void drawStatusStrip(const ThermalCoreState& core) {
    const int16_t left = MAP_LEFT + 1;
    const int16_t width = MAP_W - 2;
    const int16_t baseline = MAP_BOTTOM - 3;

    char offset[8];
    formatOffset(core, offset, sizeof(offset));

    u8g2.setDrawColor(core.openNow ? 1 : 0);
    u8g2.drawBox(left, STATUS_TOP, width, STATUS_H - 1);
    u8g2.setDrawColor(core.openNow ? 0 : 1);

    // 7x14B rather than one of the leaf vario fonts: those carry digits and a handful of unit
    // letters only, and a word set in them silently loses the glyphs it does not have.
    u8g2.setFont(u8g2_font_7x14B_tr);

    if (core.openNow) {
      u8g2.setCursor(left + 4, baseline);
      u8g2.print("OPEN");
      printRightAligned(offset, MAP_RIGHT - 4, left, baseline);
      u8g2.setDrawColor(1);
      return;
    }

    if (core.centred) {
      const char* text = "CENTRED";
      u8g2.setCursor(GLIDER_CENTER_X - u8g2.getStrWidth(text) / 2, baseline);
      u8g2.print(text);
      return;
    }

    u8g2.setCursor(left + 4, baseline);
    u8g2.print(offset);
  }

  void drawMap(const ThermalCoreState& core) {
    const int16_t gliderX = GLIDER_CENTER_X - core.turnDir * TURN_BIAS_PX;
    const float scaleM = core.mapScaleRadiusM > 1.0f ? core.mapScaleRadiusM : 40.0f;
    const float pxPerM = TURN_RADIUS_PX / scaleM;

    // Held well inside int16 before the cast.  A point far off the map is meant to clip; one that
    // has wrapped around the type reappears somewhere in the middle of the display instead.
    const auto toScreen = [](float px) {
      if (px < -4000.0f) return static_cast<int16_t>(-4000);
      if (px > 4000.0f) return static_cast<int16_t>(4000);
      return static_cast<int16_t>(roundf(px));
    };
    const auto toScreenX = [&](float rightM) { return toScreen(gliderX + rightM * pxPerM); };
    const auto toScreenY = [&](float aheadM) { return toScreen(GLIDER_Y - aheadM * pxPerM); };

    for (uint8_t i = 0; i < core.trailCount; i++) {
      const ThermalCoreTrailPoint& point = core.trail[i];
      drawTrailPoint(toScreenX(point.rightM), toScreenY(point.aheadM), point.lift);
    }

    // Once the circle is on the core the two marks sit on top of each other, and the bullseye
    // clearing its own background eats half of the centre tick.  Nothing is lost by dropping it:
    // the gap it exists to show is gone.
    if (core.circling && !core.centred) {
      drawCircleCentre(toScreenX(core.centerRightM), toScreenY(core.centerAheadM));
    }
    if (core.coreValid) {
      drawCore(toScreenX(core.coreRightM), toScreenY(core.coreAheadM));
    }

    drawGlider(gliderX, GLIDER_Y);
    if (core.coreValid) drawStatusStrip(core);
    u8g2.drawFrame(MAP_LEFT, MAP_TOP, MAP_W, MAP_H);
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
    const ThermalCoreState& core = thermalCore.state();

    display_varioBar(VARIO_BAR_TOP, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_WIDTH,
                     climbRate);
    drawAverageClimbField(core);
    drawAltitudeRow();
    drawMap(core);
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
