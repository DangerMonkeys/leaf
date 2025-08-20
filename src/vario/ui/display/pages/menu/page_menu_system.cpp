#include "ui/display/pages/menu/page_menu_system.h"

#include <Arduino.h>

#include "comms/ble.h"
#include "comms/fanet_radio.h"
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
#include "ui/display/pages/fanet/page_fanet.h"
#include "ui/display/pages/menu/system/page_menu_system_wifi.h"
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

// As the user holds the center button to reset the device, this grows from 0 to 96
uint8_t resetPending = 0;

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
          {
#else
          if (FanetRadio::getInstance().getState() == FanetRadioState::UNINSTALLED) {
#endif

            // If Fanet is not installed or supported, we should show a warning icon
            u8g2.setFont(leaf_icons);
            u8g2.print((char)0x22);
            u8g2.setFont(leaf_6x12);
            break;
          }

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
      if (state == ButtonEvent::CLICKED) settings.toggleBoolOnOff(&settings.system_autoOff);
      break;
    case cursor_system_showWarning:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolOnOff(&settings.system_showWarning);
      break;
    case cursor_system_fanet:
      if (state != ButtonEvent::CLICKED) break;
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
      if (FanetRadio::getInstance().getState() == FanetRadioState::UNINSTALLED) {
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
      if (state != ButtonEvent::CLICKED) break;

      // User has selected WiFi, show this page
      static PageMenuSystemWifi wifiPage;
      push_page(&wifiPage);
      redraw = true;
      break;
    case cursor_system_bluetooth:
      if (state != ButtonEvent::CLICKED) break;
      settings.system_bluetoothOn = !settings.system_bluetoothOn;
      if (settings.system_bluetoothOn) {
        BLE::get().start();
      } else {
        BLE::get().stop();
      }
      settings.save();
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
        mainMenuPage.backToMainMenu();
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
        settings.toggleBoolOnOff(&settings.dev_menu);

        // ..and if dev mode is now off, turn off logger settings too
        if (!settings.dev_menu) {
          settings.dev_startLogAtBoot = false;
          settings.dev_startDisconnected = false;
          // TODO: stop bus log if it's running
        }
      }
      break;
  }
}
