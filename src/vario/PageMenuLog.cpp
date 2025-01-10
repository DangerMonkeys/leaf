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
  cursor_log_autoStop,
  cursor_log_timer
};

void LogMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title(s)
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(2, 12);
    u8g2.setDrawColor(1);
    u8g2.print("LOG");
    u8g2.drawHLine(0, 15, 64);

    // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 70;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, /*105, 120,*/ 135};

    // first draw cursor selection box
    if (cursor_position ==
        cursor_log_timer) {  // extend the selection box for the timer menu item.  ("START" is wider
                             // than all the other menu choices)
      u8g2.drawRBox(setting_choice_x - 18, menu_items_y[cursor_position] - 14, 44, 16, 2);
    } else {
      u8g2.drawRBox(setting_choice_x - 4, menu_items_y[cursor_position] - 14, 30, 16, 2);
    }

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
        case cursor_log_timer:
          u8g2.setCursor(setting_choice_x - 14, menu_items_y[i]);
          if (flightTimer_isRunning())
            u8g2.print("STOP");
          else
            u8g2.print("START");
          break;
        case cursor_log_back:
          u8g2.print((char)124);
          break;
      }
      u8g2.setDrawColor(1);
    }
    display_flightTimer(10, 160, false, false);
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

      // TODO:  Enable more log formats
      LOG_FORMAT = LOG_FORMAT_KML;
      // LOG_FORMAT = (SettingLogFormat)new_val;
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
    case cursor_log_timer: {
      if (dir == Button::CENTER) {
        if (state == RELEASED && !flightTimer_isRunning())
          flightTimer_start();
        else if (state == HELD && flightTimer_isRunning())
          flightTimer_stop();
      }
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