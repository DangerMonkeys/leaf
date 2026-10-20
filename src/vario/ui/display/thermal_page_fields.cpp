#include "ui/display/thermal_page_fields.h"

#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/settings/settings.h"

namespace thermal_page {
  void drawVario(int32_t climbRate, uint8_t topOffset, uint8_t width) {
    display_varioBar(VARIO_BAR_TOP + topOffset, VARIO_BAR_HALF_HEIGHT, VARIO_BAR_HALF_HEIGHT, width,
                     climbRate);
    display_climbRatePointerTriangle(width, VARIO_BAR_TOP + topOffset + VARIO_BAR_HALF_HEIGHT, 8);
  }

  static void drawClimbRateField(int32_t climbRate, bool unitsBelow, bool large) {
    const uint8_t boxHeight = large ? 16 : CLIMB_BOX_HEIGHT;
    const uint8_t zeroY = VARIO_BAR_TOP + VARIO_BAR_HALF_HEIGHT + (large ? 2 : 0);
    const uint8_t boxY = zeroY - boxHeight / 2;
    const uint8_t boxX = large ? CORE_VARIO_BAR_WIDTH : CLIMB_BOX_X;
    u8g2.setFont(leaf_5x8);
    u8g2.setFontMode(1);
    u8g2.setDrawColor(0);
    u8g2.drawGlyph(boxX - 6, zeroY + 5, climbRate < 0 ? '-' : '+');
    u8g2.setFontMode(0);
    u8g2.setDrawColor(1);

    // The triangle shows the sign; the box shows the magnitude.
    climbRate = abs(climbRate);
    char value[12];
    const uint8_t* digitFonts[12];
    if (settings.units_climb) {
      // Keep the same ten-fpm resolution as the other flight pages.
      const int32_t fpm = min<int32_t>(climbRate * 197 / 1000 * 10, 9999);
      snprintf(value, sizeof(value),
               large        ? "%4ld"
               : fpm < 1000 ? "%03ld"
                            : "%04ld",
               static_cast<long>(fpm));
      digitFonts[0] = large ? leaf_8x14 : leaf_6x12;
      digitFonts[1] = large ? leaf_8x14 : leaf_6x10;
      digitFonts[2] = large ? leaf_6x12 : fpm < 1000 ? leaf_6x10 : leaf_5x8;
      digitFonts[3] = large ? leaf_6x12 : leaf_5x8;
    } else {
      const int32_t tenths = min<int32_t>((climbRate + 5) / 10, large ? 99 : 999);
      snprintf(value, sizeof(value), "%ld.%ld", static_cast<long>(tenths / 10),
               static_cast<long>(tenths % 10));
      for (uint8_t i = 0; value[i]; ++i) digitFonts[i] = large ? leaf_8x14 : leaf_6x12;
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
    if (!large) {
      u8g2.setFont(leaf_5h);
      const char* units = settings.units_climb ? "fpm" : "m/s";
      const uint8_t unitsY = unitsBelow ? boxY + boxHeight + 7 : boxY - 2;
      u8g2.drawStr(CLIMB_UNITS_CENTER_X - (u8g2.getStrWidth(units) + 1) / 2, unitsY, units);
    }

    u8g2.drawBox(boxX, boxY, width + 1 + (large ? 9 : 0), boxHeight);
    int16_t x = boxX;
    u8g2.setDrawColor(0);
    for (uint8_t i = 0; value[i]; ++i) {
      u8g2.setFont(digitFonts[i]);
      const uint8_t height = digitFonts[i] == leaf_8x14   ? 14
                             : digitFonts[i] == leaf_6x12 ? 12
                             : digitFonts[i] == leaf_6x10 ? 10
                                                          : 8;
      u8g2.drawGlyph(x, boxY + height + 1, value[i]);
      x += digitWidths[i];
    }
    if (large) {
      // Existing 5x12 unit glyphs fit the narrow strip after the digits.
      u8g2.setFont(leaf_6x12);
      u8g2.drawGlyph(boxX + width + 2, boxY + 14, settings.units_climb ? 132 : 131);
    }
    u8g2.setDrawColor(1);
  }

  void drawClimbRate(int32_t climbRate, bool unitsBelow) {
    drawClimbRateField(climbRate, unitsBelow, false);
  }

  void drawCoreClimbRate(int32_t climbRate) { drawClimbRateField(climbRate, false, true); }

  uint8_t drawCoreAltitude(int32_t altitude, uint8_t x, uint8_t baselineY, uint8_t* tensX) {
    altitude = constrain(altitude, -99999, 99999);
    const int32_t magnitude = abs(altitude);
    // A negative four-digit altitude also needs the compact five-character layout.
    const bool compact = magnitude >= 10000 || altitude <= -1000;
    if (compact) --baselineY;
    const uint8_t* highFont = compact ? leaf_8x14 : leaf_10x17;
    const uint8_t* lowFont = compact ? leaf_6x12 : leaf_8x14;
    char digits[8];
    snprintf(digits, sizeof(digits), compact ? "%5ld" : "%4ld", static_cast<long>(altitude));
    const uint8_t count = strlen(digits);
    u8g2.setCursor(x, baselineY);
    for (uint8_t i = 0; i < count; ++i) {
      if (i == count - 3) {
        u8g2.setFont(highFont);
        if (!compact) u8g2.setCursor(u8g2.getCursorX() - 1, baselineY);
        if (magnitude >= 1000) {
          u8g2.print(',');
        } else {
          // Reserve the comma's position even when it is hidden.
          u8g2.setCursor(u8g2.getCursorX() + u8g2_GetGlyphWidth(u8g2.getU8g2(), ','), baselineY);
        }
        if (!compact) u8g2.setCursor(u8g2.getCursorX() - 1, baselineY);
      }
      const bool small = i >= count - 2;
      if (i == count - 2 && tensX) *tensX = u8g2.getCursorX();
      u8g2.setFont(small ? lowFont : highFont);
      const uint8_t slotX = u8g2.getCursorX();
      const uint8_t advance = u8g2_GetGlyphWidth(u8g2.getU8g2(), '0');
      const uint8_t heightDifference = compact ? 2 : 3;
      u8g2.setCursor(slotX, baselineY - (small ? heightDifference : 0));
      u8g2.print(digits[i]);
      // Space and digit glyphs do not all have the same advance in these fonts.
      u8g2.setCursor(slotX + advance, baselineY);
    }
    return u8g2.getCursorX();
  }

}  // namespace thermal_page
