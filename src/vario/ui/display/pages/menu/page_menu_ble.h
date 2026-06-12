#pragma once

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

/////////////////////////////////////////
// Bluetooth Settings sub-page
//  Purpose:  Activates the BLE settings GATT service and displays the
//            pairing PIN while the page is open.  Service is disabled
//            when the page is closed.
class PageBleSettings : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Companion App"; }

  void shown() override;
  void closed(bool removed_from_stack) override;
  void draw_extra() override;

 private:
  uint32_t pin_ = 0;
};

/////////////////////////////////////////
// Main BLE Menu Page
//  Purpose:  Replaces the old Bluetooth on/off toggle with a proper
//            sub-menu: On/Off toggle + entry into the settings page.
class BleMenuPage : public SettingsMenuPage {
 public:
  BleMenuPage() {
    cursor_position = 0;
    cursor_max = 2;
  }

  void draw();
  void backToBleMenu();

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  static constexpr const char* labels[3] = {"Back", "Bluetooth", "Companion"};
  PageBleSettings page_ble_settings;
};
