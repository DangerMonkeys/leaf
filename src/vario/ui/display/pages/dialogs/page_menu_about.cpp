#include "ui/display/pages/dialogs/page_menu_about.h"

#include "WiFi.h"
#include "comms/fanet_radio.h"
#include "esp_mac.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"
#include "version.h"

void PageMenuAbout::draw_extra() {
  auto y = 40;
  constexpr auto offset = 10;

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, y += offset);
  u8g2.print("Ver: ");
  u8g2.setCursor(5, y += offset);
  u8g2.setFont(leaf_5x8);
  u8g2.print(FIRMWARE_VERSION);
  y += offset;

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, y += offset);
  u8g2.print("Ip: ");
  u8g2.setCursor(5, y += offset);
  u8g2.setFont(leaf_5x8);
  u8g2.print(WiFi.localIP().toString());
  y += offset;

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

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, y += offset);
  u8g2.print("Fanet Address: ");
  u8g2.setCursor(5, y += offset);
  u8g2.setFont(leaf_5x8);
#ifndef HAS_FANET
  u8g2.print("N/A");
#else
  u8g2.print(FanetRadio::getAddress());
#endif
  y += offset;
}

void PageMenuAbout::show() { push_page(this); }