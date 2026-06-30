#include "ui/display/pages/menu/page_menu_wifi.h"

#include <Arduino.h>

#include "comms/ble.h"
#include "comms/ota.h"
#include "comms/webserver.h"
#include "comms/wifi_coordinator.h"
#include "hardware/buttons.h"
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
  constexpr uint8_t RESET_HOLD_COUNT = 5;
}  // namespace

int wifi_menu_ui::iconForCurrentConnection() {
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

bool wifi_menu_ui::isConnectedToNamedNetwork() {
  return WiFi.status() == WL_CONNECTED && !WiFi.SSID().isEmpty();
}

void wifi_menu_ui::attemptSavedNetworkConnection() { leaf_wifi::attemptSavedNetworkConnection(); }

void wifi_menu_ui::disconnectFromNetwork() { leaf_wifi::disconnectFromNetwork(); }

void wifi_menu_ui::drawStatusLine(uint8_t y) {
  const bool savedNetworkAttemptActive = leaf_wifi::savedNetworkConnectionInProgress();
  const String connectedSsid = WiFi.SSID();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED && !connectedSsid.isEmpty();
  const bool wifiSettling = WiFi.status() == WL_CONNECTED && connectedSsid.isEmpty();

  String statusText;
  if (wifiConnected) {
    statusText = connectedSsid;
  } else if (savedNetworkAttemptActive || wifiSettling) {
    statusText = "Searching..";
  } else {
    statusText = "Not Connected";
  }

  if (statusText.length() > 13) {
    statusText = statusText.substring(0, 13);
  }

  u8g2.setDrawColor(1);
  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(0, y);
  u8g2.print(statusText);
  u8g2.setFont(leaf_icons);
  u8g2.setCursor(85, y + 2);
  u8g2.print((char)iconForCurrentConnection());
  u8g2.setFont(leaf_6x12);
}

enum wifi_menu_items_connected {
  cursor_wifi_back,
  cursor_wifi_setup,
  cursor_wifi_resetWifiSettings,
  cursor_wifi_bluetooth
};

void WifiMenuPage::draw() {
  if (firstOpened) {
    firstOpened = false;
    attemptWifiConnection();
  }

  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("Connect", menu_ui::GLYPH_CONNECTIVITY);

    // Wifi connection status and icon
    wifi_menu_ui::drawStatusLine(30);

    // Menu Items
    uint8_t setting_name_x = 2;
    uint8_t wifi_item_name_x = 10;
    uint8_t bluetooth_item_name_x = 10;
    uint8_t setting_choice_x = 70;
    uint8_t menu_items_y[] = {190, 72, 87, 133};

    u8g2.setDrawColor(1);
    menu_ui::drawLabel(setting_name_x, 55, "Wifi:", menu_ui::GLYPH_WIFI);
    u8g2.drawHLine(2, 57, 92);
    menu_ui::drawLabel(setting_name_x, 115, "Bluetooth:", menu_ui::GLYPH_BLUETOOTH);
    u8g2.drawHLine(2, 117, 92);
    if (settings.system_bluetoothOn) {
      u8g2.setFont(leaf_5x8);
      u8g2.setCursor(bluetooth_item_name_x, 148);
      u8g2.print("Name: ");
      u8g2.print(webserver_leaf_ap_ssid());
      u8g2.setFont(leaf_6x12);
    }

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      uint8_t glyph = 0;
      uint8_t label_x = setting_name_x;
      if (i == cursor_wifi_setup) {
        glyph = menu_ui::GLYPH_SETUP;
        label_x = wifi_item_name_x;
      }
      if (i == cursor_wifi_resetWifiSettings) {
        glyph = menu_ui::GLYPH_RESET;
        label_x = wifi_item_name_x;
      }
      if (i == cursor_wifi_bluetooth) {
        glyph = menu_ui::GLYPH_BLE;
        label_x = bluetooth_item_name_x;
      }
      menu_ui::drawLabel(label_x, menu_items_y[i], labels[i], glyph);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
        case cursor_wifi_back:
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
        case cursor_wifi_resetWifiSettings:
          if (selected) {
            u8g2.setCursor(menu_ui::HOLD_X, menu_items_y[i]);
            u8g2.print("HOLD");
          }
          break;
        case cursor_wifi_bluetooth:
          u8g2.setCursor(menu_ui::ICON_CHECKBOX_X, menu_items_y[i]);
          menu_ui::printGlyph(settings.system_bluetoothOn ? menu_ui::ICON_ON : menu_ui::ICON_OFF);
          break;
        case cursor_wifi_setup:
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        default:
          break;
      }
      menu_ui::endRow();
    }

    if (resetPending) {
      const uint8_t width =
          resetPending >= RESET_HOLD_COUNT ? 96 : resetPending * 96 / RESET_HOLD_COUNT;
      u8g2.drawBox(0, 95, width, 4);
    }
  } while (u8g2.nextPage());
}

void WifiMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_wifi_resetWifiSettings:
      if (state == ButtonEvent::INCREMENTED) {
        resetPending = count;
        if (count >= RESET_HOLD_COUNT) {
          buttons.consumeButton();
          resetPending = 0;
          webserver_disable_user_app();
          leaf_wifi::resetUserWifiSettings();
          wifi_state = WifiState::DISCONNECTED;
          speaker.playSound(fx::confirm);
        }
      } else if (state == ButtonEvent::RELEASED || state == ButtonEvent::CLICKED) {
        resetPending = 0;
      }
      break;
    case cursor_wifi_bluetooth:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::confirm);
        settings.system_bluetoothOn = !settings.system_bluetoothOn;
        if (settings.system_bluetoothOn) {
          BLE::get().start();
        } else {
          BLE::get().stop();
        }
        settings.save();
      }
      break;
    case cursor_wifi_setup:
      if (state == ButtonEvent::CLICKED) {
        showSetup();
      }
      break;
    case cursor_wifi_back: {
      if (state == ButtonEvent::CLICKED || state == ButtonEvent::HELD) {
        speaker.playSound(fx::cancel);
        settings.save();
        settingsMenuPage.backToSettingsMenu();
        firstOpened = true;  // reset for the next time user enters wifi menu
        if (state == ButtonEvent::HELD) {
          speaker.playSound(fx::exit);
          mainMenuPage.quitMenu();
        }
      }
      break;
    }
    default:
      break;
  }
}

void WifiMenuPage::showSetup() { push_page(&page_wifi_setup); }

void WifiMenuPage::showFirmwareUpdate() { push_page(&page_wifi_update); }

void WifiMenuPage::showWebApp() { push_page(&page_wifi_web_app); }

void PageMenuSystemWifiSetup::beginWifiSetup() { webserver_enable_wifi_setup(); }

void WifiMenuPage::attemptWifiConnection() {
  wifi_state = WifiState::CONNECTING;

  // This attempts to connect using credentials stored by the ESP WiFi stack.
  wifi_menu_ui::attemptSavedNetworkConnection();
}

namespace {
  void drawQrCodeScaled(const char* text, uint8_t xOffset, uint8_t yOffset, uint8_t version,
                        uint8_t scale) {
    static constexpr uint8_t QR_MAX_VERSION = 3;

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(QR_MAX_VERSION)];
    qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, text);

    for (uint8_t x = 0; x < qrcode.size; x++) {
      for (uint8_t y = 0; y < qrcode.size; y++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          u8g2.drawBox(xOffset + x * scale, yOffset + y * scale, scale, scale);
        }
      }
    }
  }

  void drawQrCode(const char* text, uint8_t xOffset, uint8_t yOffset) {
    drawQrCodeScaled(text, xOffset, yOffset, 3, 2);
  }

  void drawSmallQrCode(const char* text, uint8_t xOffset, uint8_t yOffset) {
    drawQrCodeScaled(text, xOffset, yOffset, 2, 2);
  }
}  // namespace

etl::array<const char*, 1> PageMenuSystemWifiWebApp::network_labels{{"Use Leaf AP"}};

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
    syncWebAppMode();
    const bool reconnectToSavedNetwork = using_leaf_wifi;
    webserver_disable_user_app();
    if (reconnectToSavedNetwork) {
      wifi_menu_ui::attemptSavedNetworkConnection();
    }
  }
}

void PageMenuSystemWifiWebApp::loop() {}

void PageMenuSystemWifiWebApp::syncWebAppMode() {
  const bool serverUsingLeafWifi = webserver_user_app_using_leaf_wifi();
  if (using_leaf_wifi == serverUsingLeafWifi) return;

  using_leaf_wifi = serverUsingLeafWifi;
  cursor_position = CURSOR_BACK;
  cursor_max = using_leaf_wifi ? -1 : network_labels.size() - 1;
}

etl::array_view<const char*> PageMenuSystemWifiWebApp::get_labels() const {
  if (using_leaf_wifi) return etl::array_view<const char*>(emptyMenu);
  return etl::array_view<const char*>(network_labels);
}

void PageMenuSystemWifiWebApp::draw() {
  syncWebAppMode();
  if (using_leaf_wifi) {
    SimpleSettingsMenuPage::draw();
    return;
  }

  u8g2.firstPage();
  do {
    menu_ui::drawTitle(get_title(), get_title_glyph());

    const auto NETWORK_ACTION_Y = 166;
    menu_ui::beginRow(190, cursor_position == CURSOR_BACK);
    menu_ui::drawLabel(2, 190, "Back");
    menu_ui::drawBackIcon(74, 190);
    menu_ui::endRow();

    menu_ui::beginRow(NETWORK_ACTION_Y, cursor_position == 0);
    u8g2.setCursor(2, NETWORK_ACTION_Y);
    menu_ui::printGlyph(menu_ui::GLYPH_WIFI);
    u8g2.print(' ');
    u8g2.setFont(leaf_5x8);
    u8g2.print(network_labels[0]);
    u8g2.setFont(leaf_6x12);
    menu_ui::drawEnterIcon(74, NETWORK_ACTION_Y, cursor_position == 0);
    menu_ui::endRow();

    draw_extra();
  } while (u8g2.nextPage());

  loop();
}

void PageMenuSystemWifiWebApp::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  syncWebAppMode();

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
    u8g2.setCursor(1, 27);
    u8g2.print("Join Wifi: ");
    u8g2.print(webserver_leaf_ap_ssid());
    u8g2.setCursor(1, 37);
    u8g2.print("Password: ");
    u8g2.print(webserver_leaf_ap_password());
    drawQrCode(webserver_leaf_ap_wifi_qr().c_str(), 19, 41);

    u8g2.setCursor(17, 109);
    u8g2.print("Open Web App:");
    drawSmallQrCode(webserver_user_app_url().c_str(), 23, 112);
    u8g2.setCursor(8, 172);
    String shortUrl = webserver_user_app_url();
    shortUrl.replace("http://", "");
    u8g2.print(shortUrl);
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
    u8g2.print((char)wifi_menu_ui::iconForCurrentConnection());

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, 58);
    u8g2.print("Open in browser:");
    drawQrCode(webserver_user_app_url().c_str(), 23, 64);

    String shortUrl = webserver_user_app_url();
    shortUrl.replace("http://", "");
    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, 134);
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
  if (WiFi.status() == WL_CONNECTED && !webserver_user_app_using_leaf_wifi()) {
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
    u8g2.print("Leaf WiFi...");

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(8, 104);
    u8g2.print("Network may take");
    u8g2.setCursor(15, 116);
    u8g2.print("10-20 seconds");
    u8g2.setCursor(8, 128);
    u8g2.print("to show on phone");
    return;
  }

  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(0, 28);
  u8g2.print("Follow these steps");
  u8g2.setCursor(0, 39);
  u8g2.print("on Phone or Laptop:");

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, 58);
  u8g2.print("1.Join:");
  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(5, 70);
  u8g2.print(webserver_leaf_ap_ssid());
  u8g2.setCursor(5, 82);
  u8g2.print("Pass ");
  u8g2.print(webserver_leaf_ap_password());

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, 103);
  u8g2.print("2.Sign In");
  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(5, 115);
  u8g2.print("Or Visit:");
  u8g2.setCursor(5, 127);
  u8g2.print("192.168.4.1/wifi");

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, 148);
  u8g2.print("3.Enter Network");
  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(5, 160);
  u8g2.print("Select network");
  u8g2.setCursor(5, 172);
  u8g2.print("and enter password");

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
