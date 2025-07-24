#include "ui/display/pages/menu/page_menu_system.h"

#include <Arduino.h>

#include "comms/ble.h"
#include "comms/fanet_radio.h"
#include "power.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/display/pages/dialogs/page_menu_about.h"
#include "ui/display/pages/dialogs/page_message.h"
#include "ui/display/pages/fanet/page_fanet.h"
#include "ui/display/pages/menu/system/page_menu_system_wifi.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum system_menu_items {
  cursor_system_back,
  cursor_system_timezone,
  cursor_system_volume,
  cursor_system_poweroff,
  cursor_system_showWarning,
  cursor_system_fanet,
  cursor_system_wifi,
  cursor_system_bluetooth,
  cursor_system_about,
  cursor_system_reset,
};

PageMenuAbout about_page;

// used for counting how long the user has held the button down to reset the settings
uint8_t reset_settings_timer = 0;

void SystemMenuPage::draw() {
  int16_t displayTimeZone = settings.system_timeZone;

  u8g2.firstPage();
  do {
    // Title
    display_menuTitle("SYSTEM");

    // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 64;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 105, 120, 135, 150, 165};
    char twoZeros[] = "00";

    // first draw cursor selection box
    u8g2.drawRBox(setting_choice_x - 2, menu_items_y[cursor_position] - 14, 34, 16, 2);

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
        case cursor_system_timezone:
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
            u8g2.print(twoZeros);
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
          u8g2.setCursor(setting_choice_x + 8, menu_items_y[i]);
          if (settings.system_autoOff)
            u8g2.print((char)125);
          else
            u8g2.print((char)123);
          break;

        case cursor_system_showWarning:
          u8g2.setCursor(setting_choice_x + 8, menu_items_y[i]);
          if (settings.system_showWarning)
            u8g2.print((char)125);
          else
            u8g2.print((char)123);
          break;

        case cursor_system_fanet:
          u8g2.setCursor(setting_choice_x + 8, menu_items_y[i]);
#ifndef FANET_CAPABLE
          // If Fanet is not supported, we should show a warning icon
          u8g2.setFont(leaf_icons);
          u8g2.print((char)0x22);
          u8g2.setFont(leaf_6x12);
          break;
#endif
          u8g2.print(settings.fanet_region == FanetRadioRegion::OFF ? (String) "OFF"
                                                                    : (String)((char)126));
          break;

        case cursor_system_bluetooth:
          u8g2.setCursor(setting_choice_x + 4, menu_items_y[i]);
          if (settings.system_bluetoothOn)
            u8g2.print("ON");
          else
            u8g2.print("OFF");
          break;

        case cursor_system_reset:
          u8g2.setCursor(setting_choice_x + 8, menu_items_y[i]);
          if (cursor_position == cursor_system_reset) {
            u8g2.setCursor(setting_choice_x, menu_items_y[i]);
            u8g2.print("HOLD");
          } else
            u8g2.print((char)126);
          break;

        case cursor_system_back:
          u8g2.setCursor(setting_choice_x + 8, menu_items_y[i]);
          u8g2.print((char)124);
          break;

        default:
          u8g2.setCursor(setting_choice_x + 8, menu_items_y[i]);
          u8g2.print((char)126);
          break;
      }
      u8g2.setDrawColor(1);
    }

    if (reset_settings_timer && buttons_get_hold_count()) {
      u8g2.drawBox(0, 170, reset_settings_timer, 4);
    } else {
      reset_settings_timer = 0;
    }

  } while (u8g2.nextPage());
}

void SystemMenuPage::setting_change(Button dir, ButtonState state, uint8_t count) {
  bool redraw = false;
  switch (cursor_position) {
    case cursor_system_timezone:
      if (state == RELEASED && dir != Button::NONE) settings.adjustTimeZone(dir);
      if (state == HELD && dir == Button::NONE) settings.adjustTimeZone(dir);
      break;
    case cursor_system_volume:
      if (state == RELEASED && dir != Button::NONE) settings.adjustVolumeSystem(dir);
      break;
    case cursor_system_poweroff:
      if (state == RELEASED) settings.toggleBoolOnOff(&settings.system_autoOff);
      break;
    case cursor_system_showWarning:
      if (state == RELEASED) settings.toggleBoolOnOff(&settings.system_showWarning);
      break;
    case cursor_system_fanet:
      if (state != RELEASED) break;
#ifndef FANET_CAPABLE
      PageMessage::show("Fanet",
                        "UNSUPPORTED\n"
                        "\n"
                        "Fanet is not\n"
                        "supported on\n"
                        "this device.\n"
                        "\n"
                        "  Sorry!\n"
                        "\n"
                        "    :(\n");
      break;
#endif
      if (FanetRadio::getInstance().getState() == FanetRadioState::UNINITIALIZED) {
        // If the FANET radio is uninstalled, show a warning message
        PageMessage::show("Fanet",
                          "Fanet radio\n"
                          "not installed.\n\n"
                          "Install radio\n"
                          "or contact\n"
                          "support\n");
      } else {
        // Show the Fanet setting page
        PageFanet::show();
      }
      break;
    case cursor_system_wifi:
      if (state != RELEASED) break;

      // User has selected WiFi, show this page
      static PageMenuSystemWifi wifiPage;
      push_page(&wifiPage);
      redraw = true;
      break;
    case cursor_system_bluetooth:
      if (state != RELEASED) break;
      settings.system_bluetoothOn = !settings.system_bluetoothOn;
      if (settings.system_bluetoothOn) {
        BLE::get().start();
      } else {
        BLE::get().stop();
      }
      settings.save();
      break;
    case cursor_system_reset:
      if (state == RELEASED || state == NO_STATE) {
        reset_settings_timer = 0;
      }
      if ((state == HELD || state == HELD_LONG) && count <= 12) {
        reset_settings_timer = count * 8;
        if (count == 12) {
          settings.reset();
          speaker_playSound(fx_confirm);
          reset_settings_timer = 0;
        }
      }
      break;
    case cursor_system_back:
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
    case cursor_system_about:
      if (state == RELEASED) {
        speaker_playSound(fx_confirm);
        about_page.show();
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