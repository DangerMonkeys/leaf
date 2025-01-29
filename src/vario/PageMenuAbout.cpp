#include "PageMenuAbout.h"
#include "WiFi.h"
#include "display.h"
#include "fonts.h"
#include "version.h"

void PageMenuAbout::draw_extra() {
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, 40);
  u8g2.print("Ver: ");
  u8g2.setCursor(5, 50);
  u8g2.setFont(leaf_5x8);
  u8g2.print(VERSION);

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, 80);
  u8g2.print("Ip: ");
  u8g2.setCursor(5, 90);
  u8g2.setFont(leaf_5x8);
  u8g2.print(WiFi.localIP().toString());

  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(0, 120);
  u8g2.print("Mac: ");
  u8g2.setCursor(5, 130);
  u8g2.setFont(leaf_5x8);
  u8g2.print(WiFi.macAddress());
}

void PageMenuAbout::show() {
  push_page(this);
}