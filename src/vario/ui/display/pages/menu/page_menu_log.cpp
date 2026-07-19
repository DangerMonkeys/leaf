#include "ui/display/pages/menu/page_menu_log.h"

#include <Arduino.h>

#include "logging/log.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum log_menu_items {
  cursor_log_back,
  cursor_log_saveLog,
  cursor_log_autoStart,
  cursor_log_autoStop,
};

enum log_menu_pages {
  page_menu_log,
  page_logbook,
};

uint8_t log_menu_page = page_menu_log;

bool LogMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
  if (log_menu_page == page_logbook) {
    return pageLogbook.button_event(button, state, count);
  }
  return SettingsMenuPage::button_event(button, state, count);
}

void LogMenuPage::backToLogMenu() {
  cursor_position = cursor_log_back;
  log_menu_page = page_menu_log;
}

void LogMenuPage::showLogbook() { pageLogbook.showModalNewest(); }

void LogMenuPage::draw() {
  switch (log_menu_page) {
    case page_menu_log:
      drawLogMenu();
      break;
    case page_logbook:
      pageLogbook.draw();
      break;
  }
}

void LogMenuPage::drawLogMenu() {
  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("Logging", menu_ui::GLYPH_LOGGING);

    // Menu Items
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 78;
    uint8_t menu_items_y[] = {190, 45, 60, 75};

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
        case cursor_log_saveLog:
          if (settings.log_saveTrack)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_log_autoStart:
          if (settings.log_autoStart)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_log_autoStop:
          if (settings.log_autoStop)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_log_back:
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void LogMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_log_saveLog: {
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        settings.toggleBoolOnOff(&settings.log_saveTrack);
      break;
    }
    case cursor_log_autoStart: {
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        settings.toggleBoolOnOff(&settings.log_autoStart);
      break;
    }
    case cursor_log_autoStop: {
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        settings.toggleBoolOnOff(&settings.log_autoStop);
      break;
    }
    case cursor_log_back: {
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        settingsMenuPage.backToSettingsMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        settingsMenuPage.quitMenu();
      }
      break;
    }
    default:
      break;
  }
}
