#include "ui/display/pages/menu/page_menu_developer.h"

#include <Arduino.h>

#include "logging/buslog.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum developer_menu_items {
  cursor_developer_back,
  cursor_developer_fanetReTx,
  cursor_developer_startupStart,
  cursor_developer_startupDisconnect,
  cursor_developer_busLogControl
};

void DeveloperMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title
    display_menuTitle("DEVELOPER");

    // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 76;
    uint8_t menu_items_y[] = {190, 35, 135, 150, 170};

    // first draw cursor selection box
    uint8_t largerChoiceSize = 0;
    if (cursor_position == cursor_developer_busLogControl) {
      largerChoiceSize = 12;  // make the bus logger control button larger
    }
    u8g2.drawRBox(setting_choice_x - 4 - largerChoiceSize, menu_items_y[cursor_position] - 14,
                  30 + largerChoiceSize, 16, 2);

    // draw Bus Logger Section
    u8g2.drawHLine(0, 105, 96);
    u8g2.setCursor(0, 120);
    u8g2.print("On Startup:");

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
        case cursor_developer_fanetReTx:
          if (settings.dev_fanetFwd)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_developer_startupStart:
          if (settings.dev_startLogAtBoot)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_developer_startupDisconnect:
          if (settings.dev_startDisconnected)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_developer_busLogControl:
          u8g2.setCursor(u8g2.getCursorX() - 18, u8g2.getCursorY());
          if (busLog.isLogging()) {
            u8g2.print("STOP");
          } else {
            u8g2.print("START");
          }
          break;
        case cursor_developer_back:
          u8g2.print((char)124);
          break;
      }
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void DeveloperMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_developer_fanetReTx: {
      if (state == ButtonEvent::CLICKED) settings.toggleBoolOnOff(&settings.dev_fanetFwd);
      break;
    }
    case cursor_developer_startupStart: {
      if (state == ButtonEvent::CLICKED) settings.toggleBoolOnOff(&settings.dev_startLogAtBoot);
      break;
    }
    case cursor_developer_startupDisconnect: {
      if (state == ButtonEvent::CLICKED) settings.toggleBoolOnOff(&settings.dev_startDisconnected);
      break;
    }
    case cursor_developer_busLogControl: {
      if (state == ButtonEvent::CLICKED) {
        if (!busLog.isLogging()) {
          if (busLog.startLog()) {
            speaker.playSound(fx::started);
          } else {
            speaker.playSound(fx::bad);
          }
        } else {
          busLog.endLog();
        }
      }
      break;
    }
    case cursor_developer_back: {
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        mainMenuPage.quitMenu();
      }
      break;
    }
    default:
      break;
  }
}
