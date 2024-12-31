#pragma once

#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager

#include "PageQR.h"
#include "WiFi.h"
#include "etl/array.h"
#include "etl/array_view.h"
#include "etl/vector.h"
#include "menu_page.h"

enum system_wifi_items { cursor_system_wifi_setup, cursor_system_wifi_update };
enum system_wifi_setup_items {
    cursor_system_wifi_setup_iphone,
    cursor_system_wifi_setup_android,
    cursor_system_wifi_setup_manual,
};

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
// WiFi Update Update Settings sub-page
//  Purpose:  Puts the system in Hot-Spot STA mode for
//            configuring WiFi on the system

class PageMenuSystemWifiManualSetup : public SimpleSettingsMenuPage {
   public:
    PageMenuSystemWifiManualSetup() {}
    const char* get_title() const override { return "Manual Wifi Setup"; }

    void loop() override;
    void shown() override;
    void draw_extra() override;

   private:
    WiFiManager wm;
};

/////////////////////////////////////////
// WiFi setup sub-page
class PageMenuSystemWifiSetup : public SimpleSettingsMenuPage {
   public:
    PageMenuSystemWifiSetup(WifiState* wifi_state)
        : wifi_state(wifi_state),
          qr_iphone(
              "iPhone",
              "https://apps.apple.com/us/app/espressif-esptouch/id1071176700"),
          qr_android("Android",
                     "https://play.google.com/store/apps/"
                     "details?id=com.fyent.esptouch.android&hl=en_US") {}
    const char* get_title() const override { return "Wifi Setup"; }
    // void draw_extra() override;
    etl::array_view<const char*> get_labels() const override {
        static etl::array labels{"iPhone", "Android", "Manual"};
        return etl::array_view<const char*>(labels);
    }

    // On enter/shown, begin the WiFi process
    void shown() override;
    void loop() override;

   protected:
    // Handle the user clicking on the sub menus
    void setting_change(Button dir, ButtonState state, uint8_t count) override;

   private:
    WifiState* wifi_state;
    PageQR qr_iphone;
    PageQR qr_android;
    PageMenuSystemWifiManualSetup manual;
};

/////////////////////////////////////////
// WiFi Update Settings sub-page
//  Purpose:  Runs an OTA update, shows the user
//            a log of the update

class PageMenuSystemWifiUpdate : public SimpleSettingsMenuPage {
   public:
    PageMenuSystemWifiUpdate(WifiState* wifi_state) : wifi_state(wifi_state) {}
    const char* get_title() const override { return "Wifi Update"; }

    // On enter/shown, begin the WiFi OTA Update Process
    void shown() override;
    void draw_extra() override;
    void loop() override;

   private:
    WifiState* wifi_state;
    etl::vector<String, 20> log_lines;  // Log of the update process
};

/////////////////////////////////////////
//  Top Level Wifi Menu Page
//  Purpose:  Shows the user settings for either
//            Configuring WiFi or running OTA

class PageMenuSystemWifi : public SimpleSettingsMenuPage {
   public:
    PageMenuSystemWifi()
        : page_wifi_setup(&wifi_state), page_wifi_update(&wifi_state) {}

    // Menu item button icons will depend on the WiFi state
    virtual void draw_menu_input(int8_t cursor_position) override;

    const char* get_title() const override { return "Wifi"; }
    etl::array_view<const char*> get_labels() const override {
        static etl::array labels{"Setup", "Update"};
        return etl::array_view<const char*>(labels);
    }
    virtual void closed(bool removed_from_stack) override;

   protected:
    void setting_change(Button dir, ButtonState state, uint8_t count) override;

   private:
    WifiState wifi_state = WifiState::DISCONNECTED;
    PageMenuSystemWifiSetup page_wifi_setup;
    PageMenuSystemWifiUpdate page_wifi_update;
};
