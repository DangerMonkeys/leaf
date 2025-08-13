#include "ui/display/pages/menu/page_menu_developer.h"

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

enum developer_menu_items {
  cursor_developer_back,
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
    uint8_t menu_items_y[] = {190, 60, 75, 135};

    // first draw cursor selection box
    uint8_t largerChoiceSize = 0;
    if (cursor_position == cursor_developer_busLogControl) {
      largerChoiceSize = 12;  // make the bus logger control button larger
    }
    u8g2.drawRBox(setting_choice_x - 4 - largerChoiceSize, menu_items_y[cursor_position] - 14,
                  30 + largerChoiceSize, 16, 2);

    // draw On Startup Category
    u8g2.setCursor(0, 45);
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
          if (0)  // if bus logger is running
            u8g2.print("STOP");
          else
            u8g2.print("START");
          break;
        case cursor_developer_back:
          u8g2.print((char)124);
          break;
      }
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void DeveloperMenuPage::setting_change(Button dir, ButtonState state, uint8_t count) {
  switch (cursor_position) {
    case cursor_developer_startupStart: {
      if (state == RELEASED) settings.toggleBoolOnOff(&settings.dev_startLogAtBoot);
      break;
    }
    case cursor_developer_startupDisconnect: {
      if (state == RELEASED) settings.toggleBoolOnOff(&settings.dev_startDisconnected);
      break;
    }
    case cursor_developer_busLogControl: {
      if (state == RELEASED) delay(1);
      break;
    }
    case cursor_developer_back: {
      if (state == RELEASED) {
        speaker.playSound(fx::cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
      } else if (state == HELD) {
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
