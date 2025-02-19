#include "PageMenuAltimeter.h"

#include <Arduino.h>

#include "baro.h"
#include "buttons.h"
#include "display.h"
#include "displayFields.h"
#include "fonts.h"
#include "gps.h"
#include "pages.h"
#include "settings.h"
#include "speaker.h"

// these are for cursor fields only 
enum altimeter_menu_items {
  cursor_altimeter_back,
  cursor_altimeter_syncGPSNow,
  cursor_altimeter_syncGPSLogStart,
  cursor_altimeter_adjustSetting,
};
uint8_t menu_items_y[] = {190, 60, 75, 165}; 

void AltimeterMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title(s)
    u8g2.setFont(leaf_6x12);
    u8g2.setFontMode(0);
    u8g2.setCursor(9, 14);
    u8g2.setDrawColor(1);
    u8g2.print("ALTIMETER");
    u8g2.drawHLine(0, 15, 96);
    u8g2.drawLine(2,15,5,0);
    u8g2.drawHLine(5,0,75);
    u8g2.drawLine(80,0,83,15);

    // First draw all the text for the labels and setting descriptions
    uint8_t label_indent = 3;
    uint8_t value_indent = 8;

    // GPS Altitude
      u8g2.drawRFrame(0,25,96,20,3);
      u8g2.setDrawColor(0);
      u8g2.drawBox(0,24,24,12);
      u8g2.setDrawColor(1);
      u8g2.setCursor(0, 35);
      u8g2.print("GPS");
      display_alt(40, 41, leaf_6x12, gps.altitude.meters()*100);
      if (UNITS_alt) u8g2.print("ft");
      else u8g2.print("m");

    // Sync Arrow pointing down
      uint8_t top_y = 44;
      uint8_t triSize = 6;
      uint8_t tri_y = 84;
      u8g2.drawBox(4, top_y, 4, tri_y - top_y - triSize);

      u8g2.drawTriangle(triSize, tri_y, 0, tri_y - triSize, triSize + triSize, tri_y - triSize);


    // Barometric Altitude
      uint8_t start_y = 100;
      // Title
        u8g2.drawRFrame(0,start_y - 6,96,56,3);
        u8g2.setDrawColor(0);
        u8g2.drawBox(0,start_y - 11,62,12);
        u8g2.setDrawColor(1);
        u8g2.setCursor(0, start_y);
        u8g2.print("Barometric");


      // Standard Altitude Value
        u8g2.setCursor(label_indent, start_y += 15);
        u8g2.print("Std:");
        display_alt(40, start_y, leaf_6x12, baro.alt);
        if (UNITS_alt) u8g2.print("ft");
        else u8g2.print("m");

      // Adjusted Altimeter Value
        u8g2.setCursor(label_indent, start_y += 15);
        u8g2.print("Adj:");
        display_alt(40, start_y, leaf_6x12, baro.altAdjusted);
        if (UNITS_alt) u8g2.print("ft");
        else u8g2.print("m");

      // Altimeter Setting
        u8g2.setCursor(label_indent, start_y += 15);
        u8g2.print("Set:");
        u8g2.setCursor(32, start_y);
        u8g2.print(baro.altimeterSetting, 3);
        u8g2.print("inHg");

    // Altimeter User Selectable Options
      
      // Sync to GPS Now
        u8g2.setCursor(15, menu_items_y[1]);
        u8g2.print("Sync Now");

      // Sync every time the log starts
        u8g2.setCursor(15, menu_items_y[2]);
        u8g2.print("@LogStart");

      // Adjust Altimeter Setting
      u8g2.setCursor(label_indent, menu_items_y[3]);
        u8g2.print("Adjust Alt");

      // Back Menu Option
        u8g2.setCursor(label_indent, menu_items_y[0]);
        u8g2.print("Back");

      // Now draw all the cursor selection options
        uint8_t setting_choice_x = 78;

        // first draw cursor selection box
        u8g2.drawRBox(setting_choice_x - 2, menu_items_y[cursor_position] - 14, 20, 16, 2);

        // then draw all the menu items
        for (int i = 0; i <= cursor_max; i++) {
          u8g2.setCursor(setting_choice_x, menu_items_y[i]);
          if (i == cursor_position)
            u8g2.setDrawColor(0);
          else
            u8g2.setDrawColor(1);
          switch (i) {
            case cursor_altimeter_syncGPSNow:
              u8g2.print((char)126);
              break;
            case cursor_altimeter_syncGPSLogStart:
              if (ALT_SYNC_GPS)
                u8g2.print(char(125));
              else
                u8g2.print(char(123));
              break;
            case cursor_altimeter_adjustSetting:
              u8g2.print('`');
              break;
            case cursor_altimeter_back:
              u8g2.print((char)124);
              break;
          }
          u8g2.setDrawColor(1);
        }
  } while (u8g2.nextPage());
}

void AltimeterMenuPage::setting_change(Button button, ButtonState state, uint8_t count) {
  switch (cursor_position) {
    case cursor_altimeter_syncGPSLogStart:
      if ((button == Button::CENTER || button == Button::RIGHT) && state == RELEASED) settings_toggleBoolNeutral(&ALT_SYNC_GPS);
      break;
    case cursor_altimeter_syncGPSNow:
      if (state == RELEASED) {
        if (settings_matchGPSAlt()) {  // successful adjustment of altimeter setting to match GPS
          speaker_playSound(fx_enter);
        } else {  // unsuccessful
        speaker_playSound(fx_cancel);
        }
      }
      break;
    case cursor_altimeter_adjustSetting:
      if (button == Button::CENTER && count == 1 &&
          state == HELD) {             // if center button held for 1 'action time'
        if (settings_matchGPSAlt()) {  // successful adjustment of altimeter setting to match GPS
                                       // altitude
          speaker_playSound(fx_enter);
        } else {  // unsuccessful
          speaker_playSound(fx_cancel);
        }
      } else if (button == Button::LEFT || button == Button::RIGHT) {
        if (state == PRESSED || state == HELD || state == HELD_LONG) {
          if (button == Button::LEFT)
            baro_adjustAltSetting(-1, count);
          else if (button == Button::RIGHT)
            baro_adjustAltSetting(1, count);
          speaker_playSound(fx_neutral);
        }
      }
      break;
    case cursor_altimeter_back:
      if (state == RELEASED) {
        speaker_playSound(fx_cancel);
        settings_save();
        mainMenuPage.backToMainMenu();
      } else if (state == HELD) {
        speaker_playSound(fx_exit);
        settings_save();
        mainMenuPage.quitMenu();
      }
      break;
  }
}
