#include "PageMenuSystemWifi.h"

#include "WiFi.h"
#include "display.h"
#include "fonts.h"
#include "ota.h"
#include "version.h"

/**************************
 * PageMenuSystemWifi (Top Level)
 */
void PageMenuSystemWifi::setting_change(Button dir, ButtonState state, uint8_t count) {
  if (state != RELEASED) return;

  // Handle updating items
  switch (cursor_position) {
    case cursor_system_wifi_setup:
      push_page(&page_wifi_setup);
      break;
    case cursor_system_wifi_update:
      push_page(&page_wifi_update);
      break;
  }

  // Call the parent class to handle the back button
  SimpleSettingsMenuPage::setting_change(dir, state, count);
}

void PageMenuSystemWifi::draw_menu_input(int8_t cursor_position) {
  String ret((char)126);  // Enter character by default
  switch (cursor_position) {
    case cursor_system_wifi_setup:
      if (wifi_state == WifiState::SMART_CONFIG_WAITING) {
        // Animation to show we're waiting for SmartConfig
        ret = ".";
        for (int i = 0; i < (millis() / 1000) % 3; i++) {
          ret += ".";
        }
      } else if (WiFi.status() == WL_CONNECTED) {
        ret = char(125);  // Connected... Probably need a checkmark or
                          // something
      } else {
        ret = char(123);  // Not Set Up Yet
      }
      break;

    case cursor_system_wifi_update:
      break;
  }

  u8g2.print(ret);
}

void PageMenuSystemWifi::closed(bool removed_from_stack) {
#ifndef DEBUG_WIFI
  if (!removed_from_stack) return;
  WiFi.disconnect();
  wifi_state = WifiState::DISCONNECTED;
  Serial.println("WiFi disconnected");
#endif
}

/**************************
 * PageMenuSystemWifiSetup (sub-page)
 */
void PageMenuSystemWifiSetup::shown() {
  SimpleSettingsMenuPage::shown();

  if (WiFi.status() == WL_CONNECTED) {
    // If we're already connected, don't do anything
    *wifi_state = WifiState::CONNECTED;
    pop_page();
    return;
  }

  // Start the WiFi process
  *wifi_state = WifiState::SMART_CONFIG_WAITING;
  WiFi.mode(WIFI_AP_STA);
  WiFi.beginSmartConfig();
}

void PageMenuSystemWifiSetup::setting_change(Button dir, ButtonState state, uint8_t count) {
  // Call the parent class to handle the back button
  SimpleSettingsMenuPage::setting_change(dir, state, count);

  if (state != RELEASED) return;

  // Handle updating items
  switch (cursor_position) {
    case cursor_system_wifi_setup_iphone:
      push_page(&qr_iphone);
      break;
    case cursor_system_wifi_setup_android:
      push_page(&qr_android);
      break;
    case cursor_system_wifi_setup_manual:
      push_page(&manual);
      break;
  }
}

void PageMenuSystemWifiSetup::loop() {
  // Check if we're connected, if so, update the state
  // and close the dialog
  if (WiFi.status() == WL_CONNECTED) {
    *wifi_state = WifiState::CONNECTED;
    pop_page();
  }
}

/**************************
 * PageMenuSystemWifiManualSetup (sub-page)
 */
void PageMenuSystemWifiManualSetup::shown() {
  SimpleSettingsMenuPage::shown();
  WiFi.mode(WIFI_STA);
  wm.setConfigPortalBlocking(false);
  wm.autoConnect("Leaf");
  wm.setConfigPortalTimeout(60);
}

void PageMenuSystemWifiManualSetup::loop() {
  // If we're connected, close the page
  if (WiFi.status() == WL_CONNECTED) {
    pop_page();
    return;
  }
  wm.process();
}

void PageMenuSystemWifiManualSetup::draw_extra() {
  u8g2.setFont(leaf_6x12);
  auto y = 40;
  const auto OFFSET = 14;  // Font is 10px high, allow for margin
  u8g2.setCursor(2, y);

  // Instruction Page
  const char* lines[] = {"Join Leaf",
                         "WiFi network",
                         " ",
                         "Configure WiFi",
                         "SSID & Password",
                         " ",
                         "Then press Save"};

  for (auto line : lines) {
    u8g2.setCursor(0, y);
    u8g2.print(line);
    y += OFFSET;
  }
}

/**************************
 * PageMenuSystemWifiUpdate (sub-page)
 */
void PageMenuSystemWifiUpdate::shown() {
  SimpleSettingsMenuPage::shown();

  // Reset the WiFi module into a disconnected
  // TODO:  Only do this if not already connected
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  *wifi_state = WifiState::CONNECTING;

  log_lines.clear();
  log_lines.push_back("*CURRENT VERSION:");
  log_lines.push_back((String) "  " + LEAF_VERSION);
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
      String latest_version;
      try {
        latest_version = getLatestVersion();
      } catch (const std::runtime_error& e) {
        log_lines.push_back("*ERROR WHILE CHECKING");
        log_lines.push_back("*");
        log_lines.push_back(e.what());
        *wifi_state = WifiState::ERROR;
        break;
      }
      if (latest_version == LEAF_VERSION) {
        log_lines.push_back("*YOU'RE UP TO DATE!");
        *wifi_state = WifiState::OTA_UP_TO_DATE;
      } else {
        log_lines.push_back("*NEW VERSION AVAILABLE!");
        log_lines.push_back("*UPDATING TO:");
        log_lines.push_back((String) "   " + latest_version);
        log_lines.push_back("(this will take a while)");
        log_lines.push_back("*WILL REBOOT WHEN DONE");
        *wifi_state = WifiState::OTA_UPDATING;
      }
    } break;
    case WifiState::OTA_UPDATING:
      try {
        // TODO:  Enable this
        PerformOTAUpdate();
      } catch (const std::runtime_error& e) {
        log_lines.push_back("*ERROR UPDATING!");
        log_lines.push_back("*");
        log_lines.push_back(e.what());
        *wifi_state = WifiState::ERROR;
      }
      break;
  }
}