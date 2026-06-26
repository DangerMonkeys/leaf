#include "ui/display/pages/menu/page_menu_wifi.h"

#include <Arduino.h>

#include "comms/ble.h"
#include "comms/ota.h"
#include "comms/webserver.h"
#include "comms/wifi_coordinator.h"
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
#include "utils/qrcodex.h"

namespace {
  int wifiIconForCurrentConnection() {
    int wifiIcon = 65;  // the "full signal" icon
    if (WiFi.status() == WL_CONNECTED) {
      const int signalStrength = WiFi.RSSI();
      if (signalStrength < -70) wifiIcon--;  // decrease one bar
      if (signalStrength < -85) wifiIcon--;  // decrease another bar
    } else {
      wifiIcon++;  // disconnected icon
    }
    return wifiIcon;
  }
}  // namespace

enum wifi_menu_items_connected {
  cursor_wifi_back,
  cursor_wifi_connectORupdateFW,
  cursor_wifi_webApp,
  cursor_wifi_resetWifiSettings
};

void WifiMenuPage::draw() {
  if (firstOpened) {
    firstOpened = false;
    attemptWifiConnection();
  }

  const String connectedSsid = WiFi.SSID();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED && !connectedSsid.isEmpty();
  const bool wifiSettling = WiFi.status() == WL_CONNECTED && connectedSsid.isEmpty();
  const bool wifiSearching = !wifiConnected && leaf_wifi::savedNetworkConnectionInProgress();
  int wifiIcon = wifiIconForCurrentConnection();

  u8g2.firstPage();
  do {
    // Title
    display_menuTitle("WiFi");

    // Wifi connection status and icon
    u8g2.setDrawColor(1);
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(0, 35);
    if (wifiConnected) {
      u8g2.print("Connected To:");
      u8g2.setCursor(0, 50);
      u8g2.print(connectedSsid);
    } else if (wifiSearching || wifiSettling) {
      u8g2.print("Searching...");
    } else {
      u8g2.print("Not Connected");
    }
    u8g2.setFont(leaf_icons);
    u8g2.setCursor(85, 50);
    u8g2.print((char)wifiIcon);
    u8g2.setFont(leaf_6x12);

    // Menu Items
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 70;
    uint8_t menu_items_y[] = {190, 90, 105, 120};

    // first draw cursor selection box
    u8g2.drawRBox(setting_choice_x - 4, menu_items_y[cursor_position] - 14, 30, 16, 2);

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      u8g2.setCursor(setting_name_x, menu_items_y[i]);
      if (i == cursor_wifi_connectORupdateFW) {
        if (wifiConnected) {
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
        webserver_disable_user_app();
        leaf_wifi::resetUserWifiSettings();
        wifi_state = WifiState::DISCONNECTED;
      }
      break;
    case cursor_wifi_webApp:
      if (state == ButtonEvent::CLICKED) {
        push_page(&page_wifi_web_app);
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
        leaf_wifi::disconnectFromNetwork();
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

void PageMenuSystemWifiSetup::beginWifiSetup() { webserver_enable_wifi_setup(); }

void WifiMenuPage::attemptWifiConnection() {
  wifi_state = WifiState::CONNECTING;

  // This attempts to connect using credentials stored by the ESP WiFi stack.
  leaf_wifi::attemptSavedNetworkConnection();
}

namespace {
  void drawQrCode(const char* text, uint8_t xOffset, uint8_t yOffset) {
    static constexpr uint8_t QR_VERSION = 2;
    static constexpr uint8_t QR_SCALE = 2;

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(QR_VERSION)];
    qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW, text);

    for (uint8_t x = 0; x < qrcode.size; x++) {
      for (uint8_t y = 0; y < qrcode.size; y++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          u8g2.drawBox(xOffset + x * QR_SCALE, yOffset + y * QR_SCALE, QR_SCALE, QR_SCALE);
        }
      }
    }
  }
}  // namespace

etl::array<const char*, 1> PageMenuSystemWifiWebApp::network_labels{{"Use LeafWiFi"}};

void PageMenuSystemWifiWebApp::start(bool useLeafWifi) {
  using_leaf_wifi = useLeafWifi || WiFi.status() != WL_CONNECTED;
  webserver_enable_user_app(using_leaf_wifi);
}

void PageMenuSystemWifiWebApp::shown() {
  start(false);
  SimpleSettingsMenuPage::shown();
}

void PageMenuSystemWifiWebApp::closed(bool removed_from_Stack) {
  if (removed_from_Stack) {
    webserver_disable_user_app();
  }
}

etl::array_view<const char*> PageMenuSystemWifiWebApp::get_labels() const {
  if (using_leaf_wifi) return etl::array_view<const char*>(emptyMenu);
  return etl::array_view<const char*>(network_labels);
}

void PageMenuSystemWifiWebApp::draw() {
  if (using_leaf_wifi) {
    SimpleSettingsMenuPage::draw();
    return;
  }

  u8g2.firstPage();
  do {
    display_menuTitle(String(get_title()));

    const auto NETWORK_ACTION_Y = 166;
    const auto BOX_X = 74 - 10;
    const auto BOX_Y = (cursor_position == CURSOR_BACK ? 190 : NETWORK_ACTION_Y) - 14;
    u8g2.drawRBox(BOX_X, BOX_Y, 34, 16, 2);

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(2, 190);
    u8g2.print("Back");
    u8g2.setCursor(74, 190);
    u8g2.setDrawColor(cursor_position == CURSOR_BACK ? 0 : 1);
    u8g2.print((char)124);
    u8g2.setDrawColor(1);

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, NETWORK_ACTION_Y);
    u8g2.print(network_labels[0]);
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(74, NETWORK_ACTION_Y);
    u8g2.setDrawColor(cursor_position == 0 ? 0 : 1);
    u8g2.print((char)126);
    u8g2.setDrawColor(1);

    draw_extra();
  } while (u8g2.nextPage());

  loop();
}

void PageMenuSystemWifiWebApp::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (cursor_position == CURSOR_BACK && state == ButtonEvent::CLICKED) {
    pop_page();
    return;
  }

  if (state != ButtonEvent::CLICKED) return;

  if (using_leaf_wifi) return;

  if (cursor_position == 0) {
    start(true);
    cursor_position = CURSOR_BACK;
    cursor_max = get_labels().size() - 1;
  }
}

void PageMenuSystemWifiWebApp::draw_extra() {
  u8g2.setFont(leaf_5x8);
  u8g2.setDrawColor(1);

  if (using_leaf_wifi) {
    u8g2.setCursor(18, 28);
    u8g2.print("Join Leaf WiFi");
    drawQrCode("WIFI:T:nopass;S:Leaf WiFi;;", 23, 34);

    u8g2.setCursor(20, 100);
    u8g2.print("Open Web App");
    drawQrCode(webserver_user_app_url().c_str(), 23, 106);
    return;
  }

  if (webserver_user_app_active()) {
    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, 28);
    u8g2.print("Connected to:");
    u8g2.setCursor(2, 40);
    u8g2.print(WiFi.SSID());
    u8g2.setFont(leaf_icons);
    u8g2.setCursor(85, 40);
    u8g2.print((char)wifiIconForCurrentConnection());

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, 58);
    u8g2.print("Open in browser:");
    drawQrCode(webserver_user_app_url().c_str(), 23, 64);

    String shortUrl = webserver_user_app_url();
    shortUrl.replace("http://", "");
    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, 124);
    u8g2.print(shortUrl);
  } else {
    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, 90);
    u8g2.print("Web App is off");
  }
}

/**************************
 * PageMenuSystemWifiManualSetup (sub-page)
 */

void PageMenuSystemWifiSetup::shown() {
  SimpleSettingsMenuPage::shown();
  starting_message_started_ms = millis();
  beginWifiSetup();
}

void PageMenuSystemWifiSetup::closed(bool removed_from_Stack) {
  if (removed_from_Stack) {
    webserver_disable_user_app();
  }
}

void PageMenuSystemWifiSetup::loop() {
  if (WiFi.status() == WL_CONNECTED) {
    pop_page();
    return;
  }
}

void PageMenuSystemWifiSetup::draw_extra() {
  // wifi searching status
  wifiIcon++;
  if (wifiIcon > 65) wifiIcon = 62;  // loop back to empty signal icon
  u8g2.setFont(leaf_icons);
  u8g2.setCursor(85, 12);
  u8g2.print((char)wifiIcon);

  if (millis() - starting_message_started_ms < STARTING_MESSAGE_MS) {
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(24, 52);
    u8g2.print("Starting");
    u8g2.setCursor(12, 69);
    u8g2.print("Leaf Wifi...");

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(8, 104);
    u8g2.print("Network may take");
    u8g2.setCursor(15, 116);
    u8g2.print("10-20 seconds");
    u8g2.setCursor(8, 128);
    u8g2.print("to show on phone");
    return;
  }

  u8g2.setFont(leaf_6x12);
  auto y = 15;
  auto x = 0;
  const auto OFFSET = 4;  // default new paragraph spacing
  u8g2.setCursor(0, y);

  // Instruction Page
  const char* lines[] = {"Follow these steps", "on Phone or Laptop:",  "1.Join Leaf WiFi",   " ",
                         "2.Click Sign In",    "  Or Visit:",          "192.168.4.1/wifi",   " ",
                         "3.Enter Network",    "Select your network",  "and enter password", " ",
                         "4.Press Save",       "This page will close", "when connected..."};

  uint8_t lineNum = 0;

  for (auto line : lines) {
    u8g2.setCursor(0, y);
    if (lineNum == 2 || lineNum == 4 || lineNum == 8 || lineNum == 12) {
      y += 14;
      x = 0;
      u8g2.setFont(leaf_6x12);
    } else if (lineNum == 0 || lineNum == 1 || lineNum == 5 || lineNum == 6 || lineNum == 9 ||
               lineNum == 10 || lineNum == 13 || lineNum == 14) {
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
