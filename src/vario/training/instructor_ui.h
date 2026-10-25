#pragma once
#include <U8g2lib.h>
#include <string.h>
#include "pilot_name.h"
#include "ui/display/fonts.h"
namespace training {
  // Same rounded, inverted row treatment as menu_ui::beginRow, without its device services.
  template <typename Display>
  void instructorRow(Display& lcd, uint8_t baseline, bool selected, uint8_t height = 17) {
    lcd.setDrawColor(1);
    if (selected) lcd.drawRBox(0, baseline - height + 1, 96, height + 1, 2);
    lcd.setDrawColor(selected ? 0 : 1);
    lcd.setFont(leaf_6x12);
  }
  template <typename Display>
  void instructorTitle(Display& lcd, const char* text) {
    lcd.setDrawColor(1);
    lcd.setFont(leaf_6x12);
    lcd.drawStr(3, 14, text);
    lcd.drawHLine(0, 17, 96);
  }
  template <typename Display>
  void instructorChoice(Display& lcd, uint8_t baseline, const char* value) {
    lcd.drawStr(93 - lcd.getStrWidth(value), baseline, value);
  }
  template <typename Display>
  void instructorNotice(Display& lcd, const char* text, uint8_t x, uint8_t baseline, uint8_t width,
                        uint8_t rows = 2) {
    lcd.setFont(leaf_5x8);
    for (uint8_t row = 0; row < rows && *text; ++row) {
      char line[24];
      size_t count = strlen(text);
      if (count >= sizeof(line)) count = sizeof(line) - 1;
      memcpy(line, text, count);
      line[count] = 0;
      while (count && lcd.getStrWidth(line) > width) line[--count] = 0;
      if (text[count]) {
        size_t space = count;
        while (space && text[space] != ' ') --space;
        if (space) {
          count = space;
          line[count] = 0;
        }
      }
      lcd.drawStr(x, baseline + row * 11, line);
      text += count;
      while (*text == ' ') ++text;
    }
  }
  template <typename Display>
  void instructorPilotLabel(Display& lcd, const char* name, uint8_t x, uint8_t baseline,
                            uint8_t width) {
    char text[PILOT_NAME_BYTES + 1];
    pilotNameForDisplay(text, name);
    lcd.setFont(leaf_6x12);
    if (lcd.getStrWidth(text) > width) {
      // Prefer a recognizable given name plus surname initial before falling back to ellipsis.
      const char* space = strchr(text, ' ');
      const char* last = strrchr(text, ' ');
      if (space && last && last[1]) {
        const size_t count = space - text;
        if (count <= PILOT_NAME_BYTES - 3) {
          text[count] = ' ';
          text[count + 1] = last[1];
          text[count + 2] = '.';
          text[count + 3] = 0;
        }
      }
      if (lcd.getStrWidth(text) > width) {
        size_t n = strlen(text);
        if (n > PILOT_NAME_BYTES - 3) n = PILOT_NAME_BYTES - 3;
        text[n] = 0;
        while (n && lcd.getStrWidth(text) + lcd.getStrWidth("...") > width) text[--n] = 0;
        strcpy(text + n, "...");
      }
    }
    lcd.drawStr(x, baseline, text);
  }
}  // namespace training
