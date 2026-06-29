#include "ui/display/pages/menu/page_menu_developer.h"

#include <Arduino.h>

#include "diagnostics/self_test/selfTest.h"
#include "hardware/buttons.h"
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
  cursor_developer_showDebugPg,
  cursor_developer_runSelfTest,
  cursor_developer_startupStart,
  cursor_developer_startupDisconnect,
  cursor_developer_busLogControl
};

void DeveloperMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("Developer", menu_ui::GLYPH_DEVELOPER);

    // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 76;
    uint8_t menu_items_y[] = {190, 35, 50, 65, 135, 150, 170};

    // draw Bus Logger Section
    u8g2.drawHLine(0, 105, 96);
    u8g2.setCursor(0, 120);
    u8g2.print("On Startup:");

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
        case cursor_developer_fanetReTx:
          if (settings.dev_fanetFwd)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_developer_showDebugPg:
          if (settings.disp_showDebugPage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_developer_runSelfTest:
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        case cursor_developer_startupStart:
          if (settings.dev_startLogAtBoot)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_developer_startupDisconnect:
          if (settings.dev_startDisconnected)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
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
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void DeveloperMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_developer_fanetReTx: {
      if (state == ButtonEvent::CLICKED) settings.toggleBoolOnOff(&settings.dev_fanetFwd);
      break;
    }
    case cursor_developer_showDebugPg: {
      if (state == ButtonEvent::CLICKED && dir == Button::CENTER)
        settings.toggleBoolOnOff(&settings.disp_showDebugPage);
      break;
    }
    case cursor_developer_runSelfTest: {
      if (state == ButtonEvent::CLICKED) {
        cursor_position = cursor_developer_back;
        selfTest.begin(false);  // start a self test (not the official production test)
      }
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
