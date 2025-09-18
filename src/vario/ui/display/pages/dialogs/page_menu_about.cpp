#include "ui/display/pages/dialogs/page_menu_about.h"

#include "WiFi.h"
#include "esp_mac.h"

#include "comms/fanet_radio.h"
#include "system/version_info.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"

void PageMenuAbout::draw_extra() {
  auto y = 24;
  constexpr auto offset = 10;

  // Leaf Version
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, y += offset);
  u8g2.print("Leaf Version:");
  u8g2.setCursor(5, y += offset);
  u8g2.setFont(leaf_5x8);
  u8g2.print(LeafVersionInfo::firmwareVersion());
  y += offset;

  // IP Address
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, y += offset);
  u8g2.print("Ip: ");
  u8g2.setCursor(5, y += offset);
  u8g2.setFont(leaf_5x8);
  u8g2.print(WiFi.localIP().toString());
  y += offset;

  // MAC Address
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  String macStr = "";
  for (int i = 0; i < 6; i++) {
    auto hexChr = String(mac[i], HEX);
    hexChr.toUpperCase();
    macStr += String(mac[i] < 16 ? "0" : "") + hexChr;
    if (i != 5) {
      macStr += ":";
    }
  }
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, y += offset);
  u8g2.print("Mac: ");
  u8g2.setCursor(5, y += offset);
  u8g2.setFont(leaf_5x8);
  u8g2.print(macStr);
  y += offset;

  // FCC ID
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, y += offset);
  u8g2.print("FCC ID:");
  u8g2.setCursor(5, y += offset);
  u8g2.setFont(leaf_5x8);
  u8g2.print("2AC7Z-ESPS3MINI1");  // ESP32-S3-MINI-1 module FCC ID
#ifdef FANET_CAPABLE
  if (fanetRadio.getState() != FanetRadioState::UNINSTALLED) {
    u8g2.setCursor(5, y += offset);
    u8g2.print("Z4T-WIO-SX1262");
    y += offset;

    // FANET Address
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(0, y += offset);
    u8g2.print("FANET Address:");
    u8g2.setCursor(5, y += offset);
    u8g2.setFont(leaf_5x8);
    u8g2.print(FanetRadio::getAddress());
  }
#endif
}

void PageMenuAbout::show() { push_page(this); }