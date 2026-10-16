#include "ui/display/thermal_page_fields.h"

#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/settings/settings.h"

namespace thermal_page {
  void drawVario(int32_t climbRate) {
    display_varioBar(VARIO_BAR_TOP, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_WIDTH,
                     climbRate);
    display_climbRatePointerTriangle(VARIO_BAR_WIDTH, VARIO_BAR_TOP + VARIO_BAR_HALF_HEIGHT, 8);
  }

  void drawClimbRate(int32_t climbRate, bool unitsBelow) {
    u8g2.setFont(leaf_5h);
    const char* units = settings.units_climb ? "fpm" : "m/s";
    const uint8_t unitsY = unitsBelow ? CLIMB_BOX_Y + CLIMB_BOX_HEIGHT + 7 : CLIMB_BOX_Y - 2;
    u8g2.drawStr(CLIMB_UNITS_CENTER_X - (u8g2.getStrWidth(units) + 1) / 2, unitsY, units);

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

}  // namespace thermal_page
