#pragma once

#include "menu_page.h"
#include "etl/array.h"
#include "etl/array_view.h"

enum system_wifi_items {
    cursor_system_wifi_setup,
    cursor_system_wifi_update
};

// WiFi setup sub-page
class PageMenuSystemWifiSetup : public SimpleSettingsMenuPage {
  public:
    const char* get_title() const override { return "Wifi Setup"; }
};

// WiFi setup sub-page
class PageMenuSystemWifiUpdate : public SimpleSettingsMenuPage {
  public:
    const char* get_title() const override { return "Wifi Update"; }
};

// Top level menu for the Wifi system
class PageMenuSystemWifi : public SimpleSettingsMenuPage {
  public:
    const char* get_title() const override { return "Wifi"; }
    etl::array_view<const char*> get_labels() const override {
      static etl::array labels{ "Setup", "Update", "Dummy" };
      return etl::array_view<const char*>(labels);
    }
  protected:
    void setting_change(Button dir, ButtonState state, uint8_t count) override;
  
  private:
    PageMenuSystemWifiSetup page_wifi_setup;
    PageMenuSystemWifiUpdate page_wifi_update;
};

