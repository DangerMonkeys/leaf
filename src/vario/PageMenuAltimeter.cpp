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

enum altimeter_menu_items {
  cursor_altimeter_back,
  cursor_altimeter_adjustSetting,
  cursor_altimeter_adjustAlt,
  cursor_altimeter_syncToGPS
};

void AltimeterMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title(s)
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(2, 12);
    u8g2.setDrawColor(1);
    u8g2.print("ALTIMETER");
    u8g2.drawHLine(0, 15, 64);

    // First draw all the text for the labels and setting descriptions
    uint8_t label_indent = 2;
    uint8_t value_indent = 8;

    uint8_t menu_items_y[] = {
        190, 80, 115, 150};  // these are for selectable fields only (doesn't apply to
                             // standard altimeter value shown, which is not selectable)

    // Standard Altitude Value
    u8g2.setCursor(label_indent, 45);
    u8g2.print("Standard Alt:");
    display_alt(value_indent, 60, leaf_6x12, baro.alt);
    if (UNITS_alt)
      u8g2.print(" ft");
    else
      u8g2.print(" m");

    // Altimeter Setting
    u8g2.setCursor(label_indent, menu_items_y[1]);
    u8g2.print("Alt Setting:");
    u8g2.setCursor(value_indent, menu_items_y[1] += 15);
    u8g2.print(baro.altimeterSetting, 3);
    u8g2.print("inHg");

    // Adjusted Altitude Value
    u8g2.setCursor(label_indent, menu_items_y[2]);
    u8g2.print("Adjusted Alt:");
    display_alt(value_indent, menu_items_y[2] += 15, leaf_6x12, baro.altAdjusted);
    if (UNITS_alt)
      u8g2.print(" ft");
    else
      u8g2.print(" m");

    // GPS Sync Setting
    u8g2.setCursor(label_indent, menu_items_y[3]);
    u8g2.print("GPS>Sync:");

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
        case cursor_altimeter_adjustSetting:
          u8g2.print('`');
          break;
        case cursor_altimeter_adjustAlt:
          u8g2.print('`');
          break;
        case cursor_altimeter_syncToGPS:
          if (ALT_SYNC_GPS)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
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
  int8_t invert = 1;  // flip direction of button actions depending if we're adjusting altimeter
                      // (height, + means go up in altitude) or altimeter setting (inHg, + means go
                      // up in pressure, which is DOWN in altitude)

  switch (cursor_position) {
    case cursor_altimeter_syncToGPS:
      if (state == RELEASED) settings_toggleBoolNeutral(&ALT_SYNC_GPS);
      break;
    case cursor_altimeter_adjustAlt:
      invert = -1;  // flip action of the buttons for changing altimeter
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
            baro_adjustAltSetting(-1 * invert, count);
          else if (button == Button::RIGHT)
            baro_adjustAltSetting(1 * invert, count);
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
