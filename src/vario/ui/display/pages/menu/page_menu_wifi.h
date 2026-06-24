#ifndef PageMenuWifi_h
#define PageMenuWifi_h

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager

#include "etl/array.h"
#include "etl/array_view.h"
#include "etl/vector.h"
#include "ui/display/menu_page.h"
#include "ui/display/pages/dialogs/page_qr.h"
#include "ui/input/buttons.h"

enum class WifiState {
  DISCONNECTED,
  CONNECTING,
  SMART_CONFIG_WAITING,
  CONNECTED,
  OTA_CHECKING_VERSION,
  OTA_UPDATING,
  OTA_UP_TO_DATE,
  ERROR
};

/////////////////////////////////////////
// WiFi Connection Setup sub-page
//  Purpose:  Puts the system in Hot-Spot STA mode for
//            configuring WiFi on the system
class PageMenuSystemWifiSetup : public SimpleSettingsMenuPage {
 public:
  PageMenuSystemWifiSetup() {}
  const char* get_title() const override { return "Wifi Setup"; }

  void loop() override;
  void shown() override;
  void draw_extra() override;

 private:
  int wifiIcon = 62;  // the "empty signal" icon
  WiFiManager wm;
  void beginWifiSetup(void);
};

/////////////////////////////////////////
// WiFi Update Settings sub-page
//  Purpose:  Runs an OTA update, shows the user
//            a log of the update
class PageMenuSystemWifiUpdate : public SimpleSettingsMenuPage {
 public:
  PageMenuSystemWifiUpdate(WifiState* wifi_state) : wifi_state(wifi_state) {}
  const char* get_title() const override { return "FW Update"; }

  // On enter/shown, begin the WiFi OTA Update Process
  void shown() override;
  void draw_extra() override;
  void loop() override;
  virtual void closed(bool removed_from_Stack) override;

 private:
  WifiState* wifi_state;
  etl::vector<String, 20> log_lines;  // Log of the update process
  String latest_version_;
};

/////////////////////////////////////////
// WiFi Web App sub-page
//  Purpose: Starts a lightweight Leaf-hosted web app
class PageMenuSystemWifiWebApp : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Web App"; }

  void draw() override;
  void shown() override;
  void closed(bool removed_from_Stack) override;
  void draw_extra() override;
  etl::array_view<const char*> get_labels() const override;

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count) override;

 private:
  static etl::array<const char*, 1> network_labels;
  bool using_leaf_wifi = false;
  PageQR page_qr;
  void start(bool useLeafWifi);
};

/////////////////////////////////////////
// Main Parent Wifi Menu Page
//
class WifiMenuPage : public SettingsMenuPage {
 public:
  WifiMenuPage() : page_wifi_update(&wifi_state) {
    cursor_position = 0;
    cursor_max = 3;
  }
  void draw();
  bool firstOpened = true;

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  static constexpr char* labels[4] = {"Back", "Setup Wifi", "Web App", "Reset Wifi"};
  WifiState wifi_state;
  PageMenuSystemWifiSetup page_wifi_setup;
  PageMenuSystemWifiUpdate page_wifi_update;
  PageMenuSystemWifiWebApp page_wifi_web_app;
  void attemptWifiConnection(void);
};

#endif
