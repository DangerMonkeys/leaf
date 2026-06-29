#include "ui/display/pages/menu/page_menu_display.h"

#include <Arduino.h>

#include "instruments/gps.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

char string_satNum[] = "00";

enum gps_menu_items { cursor_gps_back, cursor_gps_update };

void GPSMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("GPS", menu_ui::GLYPH_GPS);

    uint8_t linespacing = 10;  // for 5x8 font values like lat/lon/hdop

    // GPS constellation and lat/long
    uint8_t size = 92;
    uint8_t x = 2;
    uint8_t y = 30;

    drawConstellation(x, y, size);

    // GPS icon
    display_GPS_icon(82, 14);

    u8g2.setFont(leaf_5x8);

    // Num of Sats (Upper left Corner)
    u8g2.setCursor(0, y - 4);
    u8g2.print("Sats:");
    u8g2.setCursor(2, u8g2.getCursorY() + linespacing);
    u8g2.print(gps.satellites.value());

    // HDOP / accuracy (upper right corner)
    u8g2.setCursor(70, y - 4);
    u8g2.print("HDOP:");
    u8g2.setCursor(76, u8g2.getCursorY() + linespacing);
    u8g2.print(float(gps.hdop.value()) / 100, 2);

    // draw lat long
    u8g2.setCursor(0, size + y + linespacing + 4);
    u8g2.print("Lat:");
    u8g2.setCursor(31, u8g2.getCursorY());
    if (abs(gps.location.lat()) < 10) {
      // add extra space for single-digit latitudes to align with longitude
      u8g2.setCursor(u8g2.getCursorX() + 6, u8g2.getCursorY());
    }
    if (gps.location.lat() >= 0) u8g2.print("+");
    u8g2.print(gps.location.lat(), 7);

    u8g2.setCursor(0, u8g2.getCursorY() + linespacing);
    u8g2.print("Lon:");
    u8g2.setCursor(25, u8g2.getCursorY());
    if (abs(gps.location.lng()) < 10) {
      // add extra space for leading zeros
      u8g2.setCursor(u8g2.getCursorX() + 6, u8g2.getCursorY());
      if (abs(gps.location.lng()) < 10) {
        u8g2.setCursor(u8g2.getCursorX() + 6, u8g2.getCursorY());
      }
    }
    if (gps.location.lng() >= 0) u8g2.print("+");
    u8g2.print(gps.location.lng(), 7);

    // Menu Items
    u8g2.setFont(leaf_6x12);
    uint8_t setting_name_x = 3;
    uint8_t setting_choice_x = 74;
    uint8_t menu_items_y[] = {190, 165};

    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
        case cursor_gps_update:
          u8g2.setCursor(setting_choice_x + 4, menu_items_y[i]);
          u8g2.print(settings.gpsMode);
          break;
        case cursor_gps_back:
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void GPSMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_gps_update:

      break;
    case cursor_gps_back:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        mainMenuPage.quitMenu();
      }
  }
}

void GPSMenuPage::drawConstellation(uint8_t x, uint8_t y, uint16_t size) {
  // Draw the satellite background
  // u8g2.setDrawColor(0);
  // u8g2.drawBox(x, y, size, size);   // clear the box drawing area
  // u8g2.setDrawColor(1);
  u8g2.drawCircle(x + size / 2, y + size / 2, size / 2);  // the horizon circle
  u8g2.drawCircle(x + size / 2, y + size / 2, size / 4);  // the 45deg elevation circle

  // Draw the satellites
  for (int i = MAX_SATELLITES - 1; i >= 0; i--) {
    if (gps.satsDisplay[i].active) {
      // Sat location (on circle display)
      uint16_t radius = (90 - gps.satsDisplay[i].elevation) * size / 2 / 90;
      int16_t sat_x = sin(gps.satsDisplay[i].azimuth * PI / 180) * radius;
      int16_t sat_y = -cos(gps.satsDisplay[i].azimuth * PI / 180) * radius;

      // Draw disc
      /*
      u8g2.drawDisc(size/2+sat_x, size/2+sat_y, size/16);
      u8g2.setDrawColor(0);
      u8g2.drawCircle(size/2+sat_x, size/2+sat_y, size/16+1);
      u8g2.setDrawColor(1);
      */

      // Draw box with numbers
      uint16_t x_pos = x + size / 2 + sat_x;
      uint16_t y_pos = y + size / 2 + sat_y;

      u8g2.setFont(u8g2_font_micro_tr);  // Font for satellite numbers
      if (gps.satsDisplay[i].snr < 20) {
        u8g2.drawFrame(x_pos - 5, y_pos - 4, 11, 9);  // white box with black border if SNR is low
        u8g2.setDrawColor(0);
        u8g2.drawBox(x_pos - 4, y_pos - 3, 9, 7);  // erase the gap between frame and text
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawBox(x_pos - 4, y_pos - 3, 9, 7);  // black box if SNR is high
        u8g2.setDrawColor(0);                      // .. with white font inside box
      }

      if (i < 9) {
        u8g2.drawStr(x_pos - 3, y_pos + 3, "0");
        x_pos += 4;
      }
      u8g2.drawStr(x_pos - 3, y_pos + 3, itoa(i + 1, string_satNum, 10));
      u8g2.setDrawColor(1);
    }
  }
}
