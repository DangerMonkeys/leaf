#include "ui/display/pages/menu/page_menu_ble.h"

#include <Arduino.h>

#include "comms/ble.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum ble_menu_items {
  cursor_ble_back,
  cursor_ble_onoff,
  cursor_ble_settings,
};

//////////////////////////////////////////////////////////
// BleMenuPage

void BleMenuPage::draw() {
  u8g2.firstPage();
  do {
    display_menuTitle("Bluetooth");

    // BT status line
    u8g2.setFont(leaf_icons);
    u8g2.setCursor(2, 35);
    u8g2.print(char(settings.system_bluetoothOn ? '&' : '%'));  // bt-selected / bt icon
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(18, 35);
    u8g2.print(settings.system_bluetoothOn ? "On" : "Off");

    // Layout: back at 190, items at 90 / 105
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 70;
    uint8_t menu_items_y[] = {190, 90, 105};

    // Cursor selection box
    u8g2.drawRBox(setting_choice_x - 4, menu_items_y[cursor_position] - 14, 30, 16, 2);

    for (int i = 0; i <= cursor_max; i++) {
      u8g2.setCursor(setting_name_x, menu_items_y[i]);
      u8g2.print(labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      u8g2.setDrawColor(i == cursor_position ? 0 : 1);

      switch (i) {
        case cursor_ble_back:
          u8g2.setCursor(setting_choice_x + 4, menu_items_y[i]);
          u8g2.print((char)124);  // back arrow
          break;
        case cursor_ble_onoff:
          u8g2.setCursor(setting_choice_x + 2, menu_items_y[i]);
          u8g2.print(settings.system_bluetoothOn ? "ON" : "OFF");
          break;
        case cursor_ble_settings:
          u8g2.setCursor(setting_choice_x + 4, menu_items_y[i]);
          u8g2.print((char)(settings.system_bluetoothOn ? 126 : 123));  // right arrow or cross
          break;
      }
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void BleMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_ble_back:
      if (state == ButtonEvent::CLICKED || state == ButtonEvent::HELD) {
        speaker.playSound(fx::cancel);
        settings.save();
        systemMenuPage.backToSystemMenu();
        if (state == ButtonEvent::HELD) {
          speaker.playSound(fx::exit);
          mainMenuPage.backToMainMenu();
        }
      }
      break;

    case cursor_ble_onoff:
      if (state != ButtonEvent::CLICKED) break;
      settings.system_bluetoothOn = !settings.system_bluetoothOn;
      if (settings.system_bluetoothOn) {
        BLE::get().start();
        speaker.playSound(fx::enter);
      } else {
        BLE::get().stop();
        speaker.playSound(fx::cancel);
      }
      settings.save();
      break;

    case cursor_ble_settings:
      if (state != ButtonEvent::CLICKED) break;
      if (!settings.system_bluetoothOn) break;  // can't enter settings if BLE is off
      speaker.playSound(fx::confirm);
      push_page(&page_ble_settings);
      break;
  }
}

void BleMenuPage::backToBleMenu() {
  cursor_position = cursor_ble_back;
}

//////////////////////////////////////////////////////////
// PageBleSettings

void PageBleSettings::shown() {
  SimpleSettingsMenuPage::shown();
  pin_ = (uint32_t)random(100000, 1000000);
  BLE::get().enableSettingsService(pin_);
}

void PageBleSettings::closed(bool removed_from_stack) {
  BLE::get().disableSettingsService();
}

void PageBleSettings::draw_extra() {
  // Large BT icon centered at top of content area
  u8g2.setFont(leaf_icons);
  u8g2.setCursor(40, 32);
  u8g2.print(char('&'));  // bluetooth-selected icon

  // Instruction label
  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(2, 50);
  u8g2.print("Enter PIN in web app:");

  // 6-digit PIN in large font
  // leaf_21h: 21px tall, 13px wide per char → 6 chars = 78px
  // left margin = (96 - 78) / 2 = 9px
  char pinStr[7];
  snprintf(pinStr, sizeof(pinStr), "%06d", pin_);
  u8g2.setFont(leaf_21h);
  u8g2.setCursor(9, 85);
  u8g2.print(pinStr);

  // Status note
  u8g2.setFont(leaf_5h);
  u8g2.setCursor(4, 105);
  u8g2.print("BLE settings active");
  u8g2.setCursor(4, 114);
  u8g2.print("PIN resets on close");

  u8g2.drawHLine(0, 174, 96);
}
