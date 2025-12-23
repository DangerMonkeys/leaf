#include "ui/display/pages/menu/page_menu_wifi.h"

#include <Arduino.h>

#include "comms/ble.h"
#include "comms/ota.h"
#include "power.h"
#include "system/version_info.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum wifi_menu_items_connected {
  cursor_wifi_back,
  cursor_wifi_connectORupdateFW,
  cursor_wifi_resetWifiSettings
};

void WifiMenuPage::draw() {
  if (firstOpened) {
    firstOpened = false;
    attemptWifiConnection();
  }

  int signalStrength = 0;
  int wifiIcon = 65;  // the "full signal" icon

  u8g2.firstPage();
  do {
    // Title
    display_menuTitle("WiFi");

    // Wifi connection status and icon
    u8g2.setDrawColor(1);
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(0, 35);
    if (WiFi.status() == WL_CONNECTED) {
      u8g2.print("Connected To:");
      u8g2.setCursor(0, 50);
      u8g2.print(WiFi.SSID());
      signalStrength = WiFi.RSSI();
      if (signalStrength < -70) wifiIcon--;  // decrease one bar
      if (signalStrength < -85) wifiIcon--;  // decrease another bar
    } else {
      u8g2.print("Not Connected");
      wifiIcon++;  // index up to the disconnected icon
    }
    u8g2.setFont(leaf_icons);
    u8g2.setCursor(85, 50);
    u8g2.print((char)wifiIcon);
    u8g2.setFont(leaf_6x12);

    // Menu Items
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 70;
    uint8_t menu_items_y[] = {190, 90, 105};

    // first draw cursor selection box
    u8g2.drawRBox(setting_choice_x - 4, menu_items_y[cursor_position] - 14, 30, 16, 2);

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      u8g2.setCursor(setting_name_x, menu_items_y[i]);
      if (i == cursor_wifi_connectORupdateFW) {
        if (WiFi.status() == WL_CONNECTED) {
          u8g2.print("Update FW");
        } else {
          u8g2.print("Setup Wifi");
        }
      } else {
        u8g2.print(labels[i]);
      }
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      if (i == cursor_position)
        u8g2.setDrawColor(0);
      else
        u8g2.setDrawColor(1);
      if (i == cursor_wifi_back)
        u8g2.print((char)124);
      else
        u8g2.print((char)126);
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void WifiMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_wifi_resetWifiSettings:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::confirm);
        WiFi.disconnect(true, true);  // erase AP
        wifi_state = WifiState::DISCONNECTED;
        Serial.println("WiFi settings reset");
      }
      break;
    case cursor_wifi_connectORupdateFW:
      if (state == ButtonEvent::CLICKED) {
        if (WiFi.status() == WL_CONNECTED) {
          push_page(&page_wifi_update);
        } else {
          push_page(&page_wifi_setup);
        }
      }
      break;
    case cursor_wifi_back: {
      if (state == ButtonEvent::CLICKED || state == ButtonEvent::HELD) {
#ifndef DEBUG_WIFI
        WiFi.disconnect();
        wifi_state = WifiState::DISCONNECTED;
        Serial.println("WiFi disconnected");
#endif
        speaker.playSound(fx::cancel);
        settings.save();
        systemMenuPage.backToSystemMenu();
        firstOpened = true;  // reset for the next time user enters wifi menu
        if (state == ButtonEvent::HELD) {
          speaker.playSound(fx::exit);
          mainMenuPage.backToMainMenu();
        }
      }
      break;
    }
    default:
      break;
  }
}

void PageMenuSystemWifiSetup::beginWifiSetup() {
  // TODO: adding these three lines increased reliability of captive portal.  not sure why
  WiFi.mode(WIFI_AP_STA);
  WiFi.beginSmartConfig();
  delay(500);

  WiFi.mode(WIFI_STA);
  wm.setConfigPortalBlocking(false);
  wm.autoConnect("Leaf WiFi");
  wm.setConfigPortalTimeout(60);
}

void WifiMenuPage::attemptWifiConnection() {
  wifi_state = WifiState::CONNECTING;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  // This attempts to connect using credentials stored by WiFiManager
  WiFi.begin();
}

/**************************
 * PageMenuSystemWifiManualSetup (sub-page)
 */

void PageMenuSystemWifiSetup::shown() {
  SimpleSettingsMenuPage::shown();
  beginWifiSetup();
}

void PageMenuSystemWifiSetup::loop() {
  // If we're connected, or portal times out, close the page
  if (WiFi.status() == WL_CONNECTED || wm.getConfigPortalActive() == false) {
    pop_page();
    return;
  }
  wm.process();
}

void PageMenuSystemWifiSetup::draw_extra() {
  // wifi searching status
  wifiIcon++;
  if (wifiIcon > 65) wifiIcon = 62;  // loop back to empty signal icon
  u8g2.setFont(leaf_icons);
  u8g2.setCursor(85, 12);
  u8g2.print((char)wifiIcon);

  u8g2.setFont(leaf_6x12);
  auto y = 20;
  auto x = 0;
  const auto OFFSET = 9;  // default new paragraph spacing
  u8g2.setCursor(0, y);

  // Instruction Page
  const char* lines[] = {">Join Leaf WiFi",     "On Phone or Laptop", " ", ">Click Sign In",
                         "  Or Visit:",         "http://192.168.4.1", " ", ">Configure WiFi",
                         "Select your network", "and enter password", " ", ">Press Save"};

  uint8_t lineNum = 0;

  for (auto line : lines) {
    u8g2.setCursor(0, y);
    if (lineNum == 0 || lineNum == 3 || lineNum == 7 || lineNum == 11) {
      y += 14;
      x = 0;
      u8g2.setFont(leaf_6x12);
    } else if (lineNum == 1 || lineNum == 4 || lineNum == 5 || lineNum == 8 || lineNum == 9) {
      y += 11;
      x = 5;
      u8g2.setFont(leaf_5x8);
    } else {
      y += OFFSET;
      u8g2.setFont(leaf_5x8);
    }
    u8g2.setCursor(x, y);
    u8g2.print(line);
    lineNum++;
  }

  u8g2.drawHLine(0, 174, 96);
}

/**************************
 * PageMenuSystemWifiUpdate (sub-page)
 */
void PageMenuSystemWifiUpdate::shown() {
  SimpleSettingsMenuPage::shown();

  // Performing an OTA update check is super expensive.
  // Unload BLE to do this check and reboot when done.
  BLE::get().end();

  // Reset the WiFi module into a disconnected
  // TODO:  Only do this if not already connected
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  *wifi_state = WifiState::CONNECTING;

  log_lines.clear();
  log_lines.push_back("*CURRENT VERSION:");
  log_lines.push_back((String) "  " + LeafVersionInfo::firmwareVersion());
  log_lines.push_back("*CONNECTING TO WIFI...");
}

void PageMenuSystemWifiUpdate::draw_extra() {
  // Draw the current log
  u8g2.setFont(leaf_5h);
  auto y = 40;
  const auto OFFSET = 7;  // Font is 5px high, allow for margin
  u8g2.setCursor(2, y);

  for (auto line : log_lines) {
    u8g2.setCursor(2, y);
    u8g2.print(line);
    y += OFFSET;
  }
}

void PageMenuSystemWifiUpdate::loop() {
  // Update the state of the OTA Updater
  switch (*wifi_state) {
    case WifiState::CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        log_lines.push_back("*CHECKING FOR UPDATES...");
        *wifi_state = WifiState::OTA_CHECKING_VERSION;
      }
      break;
    case WifiState::OTA_CHECKING_VERSION: {
      try {
        latest_version_ = getLatestTagVersion();
      } catch (const std::runtime_error& e) {
        log_lines.push_back("*ERROR WHILE CHECKING");
        log_lines.push_back("*");
        log_lines.push_back(e.what());
        *wifi_state = WifiState::ERROR;
        break;
      }
      if (latest_version_ == LeafVersionInfo::tagVersion() && !LeafVersionInfo::otaAlwaysUpdate()) {
        log_lines.push_back("*YOU'RE UP TO DATE!");
        log_lines.push_back("*REBOOT REQUIRED");
        *wifi_state = WifiState::OTA_UP_TO_DATE;
      } else {
        log_lines.push_back("*NEW VERSION AVAILABLE!");
        log_lines.push_back("*UPDATING TO:");
        log_lines.push_back((String) "   " + latest_version_ + " for " +
                            LeafVersionInfo::hardwareVariant());
        log_lines.push_back("(this will take a while)");
        log_lines.push_back("*WILL REBOOT WHEN DONE");
        *wifi_state = WifiState::OTA_UPDATING;
      }
    } break;
    case WifiState::OTA_UPDATING:
      try {
        PerformOTAUpdate(latest_version_.c_str());
      } catch (const std::runtime_error& e) {
        log_lines.push_back("*ERROR UPDATING!");
        log_lines.push_back("*");
        log_lines.push_back(e.what());
        *wifi_state = WifiState::ERROR;
      }
      break;
  }
  SimpleSettingsMenuPage::loop();
}

void PageMenuSystemWifiUpdate::closed(bool removed_from_Stack) {
  // When the page is closed, perform reboot as we unloaded things
  // that are loaded on startup like BLE.  Poor mans re-init
  // approach
  Serial.println("Rebooting the device");
  settings.boot_toOnState = true;  // restart into 'on' state on reboot
  settings.save();
  ESP.restart();  // TODO: this can't actually turn-back on the device if the user isn't holding the
                  // power button.  The ESP32 has to be on via power-button to then latch its own
                  // power supply
}
