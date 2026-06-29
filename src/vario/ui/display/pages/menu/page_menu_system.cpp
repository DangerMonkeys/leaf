#include "ui/display/pages/menu/page_menu_system.h"

#include <Arduino.h>

#include "hardware/buttons.h"
#include "power.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/display/pages/dialogs/page_menu_about.h"
#include "ui/display/pages/dialogs/page_message.h"
#include "ui/display/pages/menu/page_menu_wifi.h"
#include "ui/settings/settings.h"

enum system_menu_items {
  cursor_system_back,
  cursor_system_timezone,
  cursor_system_volume,
  cursor_system_poweroff,
  cursor_system_showWarning,
  cursor_system_about,
  cursor_system_updateFW,
  cursor_system_reset,
};

PageMenuAbout about_page;

// As the user holds the center button to reset the device, this grows from 0 to 96
uint8_t resetPending = 0;

bool SystemMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
  return SettingsMenuPage::button_event(button, state, count);
}

void SystemMenuPage::backToSystemMenu() { cursor_position = cursor_system_back; }

void SystemMenuPage::focusUpdate() { cursor_position = cursor_system_updateFW; }

void SystemMenuPage::draw() { drawSystemMenu(); }

void SystemMenuPage::drawSystemMenu() {
  int16_t displayTimeZone = settings.system_timeZone;
  const bool compactTimeZone = displayTimeZone >= 600 || displayTimeZone <= -600;

  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("System", menu_ui::GLYPH_SETTINGS);

    // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 64;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 120, 135, 165};

    // then draw all the menu items
    u8g2.setFont(leaf_6x12);
    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      const char* label = labels[i];
      if (i == cursor_system_timezone && compactTimeZone) {
        label = "TimeZone";
      }
      if (i == cursor_system_updateFW && selected && !wifi_menu_ui::isConnectedToNamedNetwork()) {
        label = "Setup Wifi?";
      }
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], label);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
        case cursor_system_timezone:
          if (compactTimeZone) {
            u8g2.setCursor(setting_choice_x - 6, menu_items_y[i]);
          }
          // sign
          if (displayTimeZone < 0) {
            u8g2.print('-');
            displayTimeZone *= -1;
          } else {
            u8g2.print('+');
          }
          // hours, :, minute
          u8g2.print(displayTimeZone / 60);
          u8g2.print(':');
          if (displayTimeZone % 60 == 0)
            u8g2.print("00");
          else
            u8g2.print(displayTimeZone % 60);
          break;

        case cursor_system_volume:
          u8g2.setCursor(setting_choice_x + 12, menu_items_y[i]);
          u8g2.setFont(leaf_icons);
          u8g2.print(char('I' + settings.system_volume));
          u8g2.setFont(leaf_6x12);
          break;

        case cursor_system_poweroff:
          u8g2.setCursor(setting_choice_x + 4, menu_items_y[i]);
          if (settings.system_autoOff) {
            if (settings.system_autoOff < 10) u8g2.print(" ");
            u8g2.print(settings.system_autoOff);
            u8g2.print("m");
          } else {
            u8g2.print("OFF");
          }
          break;

        case cursor_system_showWarning:
          u8g2.setCursor(setting_choice_x + 6, menu_items_y[i]);

          u8g2.setFont(leaf_icons);
          u8g2.print((char)34);
          u8g2.setFont(leaf_6x12);
          u8g2.setCursor(u8g2.getCursorX() + 1, u8g2.getCursorY());

          if (settings.system_showWarning)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;

        case cursor_system_reset:
          u8g2.setCursor(setting_choice_x + 8, menu_items_y[i]);
          if (cursor_position == cursor_system_reset) {
            u8g2.setCursor(menu_ui::HOLD_X, menu_items_y[i]);
            u8g2.print("HOLD");
          }
          break;

        case cursor_system_back:
          u8g2.setCursor(setting_choice_x + 8, menu_items_y[i]);
          menu_ui::drawBackIcon(setting_choice_x + 8, menu_items_y[i]);
          break;

        case cursor_system_about:
        case cursor_system_updateFW:
          menu_ui::drawEnterIcon(setting_choice_x + 8, menu_items_y[i], selected);
          break;

        default:
          break;
      }
      menu_ui::endRow();
    }

    if (resetPending) {
      u8g2.drawBox(0, 170, resetPending, 4);
    }

  } while (u8g2.nextPage());
}

void SystemMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  bool redraw = false;
  switch (cursor_position) {
    case cursor_system_timezone:
      if (state == ButtonEvent::CLICKED || state == ButtonEvent::INCREMENTED)
        settings.adjustTimeZone(dir);
      break;
    case cursor_system_volume:
      if (state == ButtonEvent::CLICKED) settings.adjustVolumeSystem(dir);
      break;
    case cursor_system_poweroff:
      if (state == ButtonEvent::CLICKED) settings.adjustAutoOff(dir);
      break;
    case cursor_system_showWarning:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolOnOff(&settings.system_showWarning);
      break;
    case cursor_system_reset:
      if (state == ButtonEvent::INCREMENTED) {
        resetPending = count * 8;
        if (count == 12) {
          buttons.consumeButton();
          settings.reset();
          speaker.playSound(fx::confirm);
        }
      } else {
        resetPending = 0;
      }
      break;
    case cursor_system_back:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        settingsMenuPage.backToSettingsMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        mainMenuPage.quitMenu();
      }
      break;
    case cursor_system_about:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::confirm);
        about_page.show();
      } else if (state == ButtonEvent::INCREMENTED && count == 4) {
        // toggle developer mode
        settings.toggleBoolOnOff(&settings.dev_mode);

        // ..and if dev mode is now off, restore default dev settings
        if (!settings.dev_mode) {
          settings.dev_startLogAtBoot = false;
          settings.dev_startDisconnected = false;
          settings.disp_showDebugPage = false;
          settings.dev_fanetFwd = true;
          settings.save();
          // TODO: stop bus log if it's running
        }
      }
      break;
    case cursor_system_updateFW:
      if (state == ButtonEvent::CLICKED) {
        if (wifi_menu_ui::isConnectedToNamedNetwork()) {
          wifiMenuPage.showFirmwareUpdate();
        } else {
          focusUpdate();
          wifiMenuPage.showSetup();
        }
      }
      break;
  }
}
