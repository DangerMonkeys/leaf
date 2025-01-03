#include "PageMenuVario.h"

#include <Arduino.h>

#include "baro.h"
#include "buttons.h"
#include "display.h"
#include "fonts.h"
#include "gps.h"
#include "pages.h"
#include "settings.h"
#include "speaker.h"

enum vario_menu_items {
  cursor_vario_back,
  cursor_vario_volume,
  cursor_vario_sensitive,
  cursor_vario_tones,
  cursor_vario_liftyair,
  cursor_vario_climbavg,
  cursor_vario_climbstart,
  cursor_vario_sinkalarm,
};

void VarioMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title(s)
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(2, 12);
    u8g2.setDrawColor(1);
    u8g2.print("VARIO");
    u8g2.drawHLine(0, 15, 64);

    // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 74;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 105, 120, 135};

    // first draw cursor selection box
    u8g2.drawRBox(setting_choice_x - 2, menu_items_y[cursor_position] - 14, 24, 16, 2);

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      u8g2.setCursor(setting_name_x, menu_items_y[i]);
      u8g2.print(labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      if (i == cursor_position)
        u8g2.setDrawColor(0);
      else
        u8g2.setDrawColor(1);
      switch (i) {
        case cursor_vario_volume:
          u8g2.print(VOLUME_VARIO);
          break;
        case cursor_vario_sensitive:
          u8g2.print(VARIO_AVERAGE);
          break;
        case cursor_vario_tones:
          u8g2.print("!!");
          break;
        case cursor_vario_liftyair:
          u8g2.print(LIFTY_AIR);
          break;
        case cursor_vario_climbavg:
          u8g2.print(CLIMB_AVERAGE);
          break;
        case cursor_vario_climbstart:
          u8g2.print(CLIMB_START);
          break;
        case cursor_vario_sinkalarm:
          u8g2.print(SINK_ALARM);
          break;
        case cursor_vario_back:
          u8g2.print((char)124);
          break;
      }
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void VarioMenuPage::setting_change(Button dir, ButtonState state, uint8_t count) {
  switch (cursor_position) {
    case cursor_vario_volume:
      if (state != RELEASED) return;
      settings_adjustVolumeVario(dir);
      break;
    case cursor_vario_sensitive:
      if (state == RELEASED) settings_adjustVarioAverage(dir);
      break;
    case cursor_vario_tones:
      if (state == RELEASED) settings_toggleBoolNeutral(&VARIO_TONES);
      break;
    case cursor_vario_liftyair:
      if (state == RELEASED) settings_adjustLiftyAir(dir);
      break;
    case cursor_vario_climbavg:
      if (state == RELEASED) settings_adjustClimbAverage(dir);
      break;
    case cursor_vario_climbstart:
      if (state == RELEASED) settings_adjustClimbStart(dir);
      break;
    case cursor_vario_sinkalarm:
      if (state == RELEASED) settings_adjustSinkAlarm(dir);
      break;
    case cursor_vario_back:
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

// helpful switch constructors to copy-paste as needed:
/*
switch (button) {
  case Button::UP:
    break;
  case Button::DOWN:
    break;
  case Button::LEFT:
    break;
  case Button::RIGHT:
    break;
  case Button::CENTER:
    break;
*/

/*
switch (state) {
  case RELEASED:
    break;
  case PRESSED:
    break;
  case HELD:
    break;
  case HELD_LONG:
    break;
}
*/