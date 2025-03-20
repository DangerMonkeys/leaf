#include "ui/display/pages/menu/page_menu_altimeter.h"

#include <Arduino.h>

#include "instruments/baro.h"
#include "instruments/gps.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

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
    display_menuTitle("ALTIMETER");

    // First draw all the text for the labels and setting descriptions
    uint8_t label_indent = 3;
    uint8_t value_indent = 8;

    // GPS Altitude
    u8g2.drawRFrame(0, 25, 96, 20, 3);
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 24, 24, 12);
    u8g2.setDrawColor(1);
    u8g2.setCursor(0, 35);
    u8g2.print("GPS");
    display_alt(40, 41, leaf_6x12, gps.altitude.meters() * 100);
    if (settings.units_alt)
      u8g2.print("ft");
    else
      u8g2.print("m");

    // Sync Arrow pointing down
    uint8_t top_y = 44;
    uint8_t triSize = 6;
    uint8_t tri_y = 84;
    u8g2.drawBox(4, top_y, 4, tri_y - top_y - triSize);

    u8g2.drawTriangle(triSize, tri_y, 0, tri_y - triSize, triSize + triSize, tri_y - triSize);

    // Barometric Altitude
    uint8_t start_y = 100;
    // Title
    u8g2.drawRFrame(0, start_y - 6, 96, 56, 3);
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, start_y - 11, 62, 12);
    u8g2.setDrawColor(1);
    u8g2.setCursor(0, start_y);
    u8g2.print("Barometric");

    // Standard Altitude Value
    u8g2.setCursor(label_indent, start_y += 15);
    u8g2.print("Std:");
    display_alt(40, start_y, leaf_6x12, baro.alt);
    if (settings.units_alt)
      u8g2.print("ft");
    else
      u8g2.print("m");

    // Adjusted Altimeter Value
    u8g2.setCursor(label_indent, start_y += 15);
    u8g2.print("Adj:");
    display_alt(40, start_y, leaf_6x12, baro.altAdjusted);
    if (settings.units_alt)
      u8g2.print("ft");
    else
      u8g2.print("m");

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
          if (settings.vario_altSyncToGPS)
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
      if ((button == Button::CENTER || button == Button::RIGHT) && state == RELEASED)
        settings.toggleBoolNeutral(&settings.vario_altSyncToGPS);
      break;
    case cursor_altimeter_syncGPSNow:
      if (state == RELEASED) {
        if (baro.syncToGPSAlt()) {  // successful adjustment of altimeter setting to match GPS
          speaker_playSound(fx_enter);
        } else {  // unsuccessful
          speaker_playSound(fx_cancel);
        }
      }
      break;
    case cursor_altimeter_adjustSetting:
      if (button == Button::CENTER && count == 1 &&
          state == HELD) {          // if center button held for 1 'action time'
        if (baro.syncToGPSAlt()) {  // successful adjustment of altimeter setting to match GPS
                                    // altitude
          speaker_playSound(fx_enter);
        } else {  // unsuccessful
          speaker_playSound(fx_cancel);
        }
      } else if (button == Button::LEFT || button == Button::RIGHT) {
        if (state == PRESSED || state == HELD || state == HELD_LONG) {
          if (button == Button::LEFT)
            baro.adjustAltSetting(-1, count);
          else if (button == Button::RIGHT)
            baro.adjustAltSetting(1, count);
          speaker_playSound(fx_neutral);
        }
      }
      break;
    case cursor_altimeter_back:
      if (state == RELEASED) {
        speaker_playSound(fx_cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
      } else if (state == HELD) {
        speaker_playSound(fx_exit);
        settings.save();
        mainMenuPage.quitMenu();
      }
      break;
  }
}
