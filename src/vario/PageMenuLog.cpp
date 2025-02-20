#include "PageMenuLog.h"

#include <Arduino.h>

#include "buttons.h"
#include "display.h"
#include "displayFields.h"
#include "fonts.h"
#include "log.h"
#include "pages.h"
#include "settings.h"
#include "speaker.h"

enum log_menu_items {
  cursor_log_back,
  cursor_log_format,
  cursor_log_saveLog,
  cursor_log_autoStart,
  cursor_log_autoStop
};

void LogMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title
    display_menuTitle("LOG/TIMER");

    // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 70;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, /*105, 120,*/ 135};

    // first draw cursor selection box
      u8g2.drawRBox(setting_choice_x - 4, menu_items_y[cursor_position] - 14, 30, 16, 2);

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
        case cursor_log_format:
          if (LOG_FORMAT == LOG_FORMAT_KML)
            u8g2.print("KML");
          else if (LOG_FORMAT == LOG_FORMAT_IGC)
            u8g2.print("IGC");
          else
            u8g2.print("_?_");
          break;
        case cursor_log_saveLog:
          if (TRACK_SAVE)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_log_autoStart:
          if (AUTO_START)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_log_autoStop:
          if (AUTO_STOP)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_log_back:
          u8g2.print((char)124);
          break;
      }
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void LogMenuPage::setting_change(Button dir, ButtonState state, uint8_t count) {
  switch (cursor_position) {
    case cursor_log_format: {
      if(state != PRESSED) {
        return;
      }
      auto new_val = (int8_t)LOG_FORMAT;
      if (dir == Button::RIGHT) {
        new_val++;
      } else if (dir == Button::LEFT) {
        new_val--;
      }
      if (new_val > SETTING_LOG_FORMAT_ENTRIES - 1) {
        new_val = 0;
      }
      if (new_val < 0) {
        new_val = SETTING_LOG_FORMAT_ENTRIES - 1;
      }

      LOG_FORMAT = (SettingLogFormat)new_val;
      break;
    }
    case cursor_log_saveLog: {
      if (state == RELEASED) settings_toggleBoolOnOff(&TRACK_SAVE);
      break;
    }
    case cursor_log_autoStart: {
      if (state == RELEASED) settings_toggleBoolOnOff(&AUTO_START);
      break;
    }
    case cursor_log_autoStop: {
      if (state == RELEASED) settings_toggleBoolOnOff(&AUTO_STOP);
      break;
    }
    case cursor_log_back: {
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
    default:
      break;
  }
}
