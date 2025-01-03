#include "PageThermalSimple.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include "IMU.h"
#include "SDcard.h"
#include "baro.h"
#include "buttons.h"
#include "display.h"
#include "displayFields.h"
#include "fonts.h"
#include "gps.h"
#include "log.h"
#include "power.h"
#include "settings.h"
#include "speaker.h"
#include "tempRH.h"

enum thermalSimple_page_items {
  cursor_thermalSimplePage_none,
  cursor_thermalSimplePage_alt1,
  // cursor_thermalSimplePage_alt2,
  cursor_thermalSimplePage_userField1,
  // cursor_thermalSimplePage_userField2,
  cursor_thermalSimplePage_timer
};
uint8_t thermalSimple_page_cursor_max = 3;

int8_t thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
uint8_t thermalSimple_page_cursor_timeCount =
    0;  // count up for every page_draw, and if no button is pressed, then reset cursor to "none"
        // after the timeOut value is reached.
uint8_t thermalSimple_page_cursor_timeOut =
    8;  // after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.

float test_wind_angle = 0;

void thermalSimplePage_draw() {
  // if cursor is selecting something, count toward the timeOut value before we reset cursor
  if (thermalSimple_page_cursor_position != cursor_thermalSimplePage_none &&
      thermalSimple_page_cursor_timeCount++ >= thermalSimple_page_cursor_timeOut) {
    thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
    thermalSimple_page_cursor_timeCount = 0;
  }

  u8g2.firstPage();
  do {
    // draw all status icons, clock, timer, etc (and pass along if timer is selected)
    display_headerAndFooter(false,
                            (thermalSimple_page_cursor_position == cursor_thermalSimplePage_timer));

    // wind
    u8g2.drawDisc(49, 25, 12);
    u8g2.setDrawColor(0);
    display_windSock(49, 25, 10, test_wind_angle);  // 0.78);
    u8g2.setDrawColor(1);

    test_wind_angle += .1;
    if (test_wind_angle > 2 * PI) test_wind_angle -= (2 * PI);

    // Main Info ****************************************************
    uint8_t topOfFrame = 22;
    uint8_t varioBarWidth = 25;
    uint8_t varioBarHeight = 151;

    // Vario Bar
    display_varioBar(topOfFrame, varioBarHeight, varioBarWidth, baro.climbRateFiltered);

    // Altitude
    uint8_t alt_y = 50;
    // Altitude header labels
    u8g2.setFont(leaf_labels);
    u8g2.setCursor(varioBarWidth + 52, alt_y);
    print_alt_label(THMPG_ALT_TYP);
    u8g2.setCursor(varioBarWidth + 40, alt_y);
    if (UNITS_alt)
      u8g2.print("ft");
    else
      u8g2.print("m");

    // Alt value
    display_alt_type(varioBarWidth + 2, alt_y + 21, leaf_21h, THMPG_ALT_TYP);

    // if selected, draw the box around it
    if (thermalSimple_page_cursor_position == cursor_thermalSimplePage_alt1) {
      display_selectionBox(varioBarWidth + 1, alt_y - 1, 96 - (varioBarWidth + 1), 24, 7);
    }

    // Climb
    display_climbRatePointerBox(varioBarWidth, 84, 76, 27, 13);  // x, y, w, h, triangle size
    display_climbRate(20, 108, leaf_21h, baro.climbRateFiltered);
    u8g2.setDrawColor(0);
    u8g2.setFont(leaf_5h);
    u8g2.print(" ");  // put a space, but using a small font so the space isn't too wide
    u8g2.setFont(leaf_21h);
    if (UNITS_climb)
      u8g2.print('f');
    else
      u8g2.print('m');
    u8g2.setDrawColor(1);

    // Alt 2 (user alt field; above launch, etc)
    display_altAboveLaunch(varioBarWidth + 4, 136, baro.altAboveLaunch);

    /* if (selected) {
u8g2.drawRFrame(cursor_x, cursor_y-16, 96-cursor_x, 18, 3);
}
    */

    // User Field ****************************************************
    uint8_t userfield_y = 171;
    switch (THMSPG_USR1) {
      case static_cast<int>(ThermSimpPageUserField1::GLIDE):
        // Glide Ratio
        u8g2.setCursor(varioBarWidth + 4, userfield_y - 14);
        u8g2.setFont(leaf_5h);
        u8g2.print("GLIDE");
        display_glide(varioBarWidth + 24, userfield_y, gps_getGlideRatio());
        break;
      case static_cast<int>(ThermSimpPageUserField1::TEMP):
        // Temperature
        u8g2.setCursor(varioBarWidth + 4, userfield_y - 14);
        u8g2.setFont(leaf_5h);
        u8g2.print("TEMP");
        display_temp(varioBarWidth + 6, userfield_y, (int16_t)tempRH_getTemp());
        // Humidity
        u8g2.setCursor(varioBarWidth + 36, userfield_y - 14);
        u8g2.setFont(leaf_5h);
        u8g2.print("HUMID");
        display_humidity(varioBarWidth + 38, userfield_y, (uint8_t)tempRH_getHumidity());

        break;
      case static_cast<int>(ThermSimpPageUserField1::ACCEL):
        // Acceleration
        u8g2.setCursor(varioBarWidth + 4, userfield_y - 14);
        u8g2.setFont(leaf_5h);
        u8g2.print("ACCEL (G FORCE)");
        display_accel(varioBarWidth + 24, userfield_y, IMU_getAccel());
        break;
      case static_cast<int>(ThermSimpPageUserField1::DIST):
        // Distance
        u8g2.setCursor(varioBarWidth + 4, userfield_y - 14);
        u8g2.setFont(leaf_5h);
        u8g2.print("DISTANCE FLOWN");
        u8g2.setFont(leaf_6x12);
        display_distance(varioBarWidth + 24, userfield_y, 0.0);
        break;
    }

    // if selected, draw the box around it
    if (thermalSimple_page_cursor_position == cursor_thermalSimplePage_userField1) {
      display_selectionBox(varioBarWidth + 1, userfield_y - 21, 96 - (varioBarWidth + 1), 23, 6);
    }

    // Footer Info ****************************************************

  } while (u8g2.nextPage());
}

void thermalSimple_page_cursor_move(Button button) {
  if (button == Button::UP) {
    thermalSimple_page_cursor_position--;
    if (thermalSimple_page_cursor_position < 0)
      thermalSimple_page_cursor_position = thermalSimple_page_cursor_max;
  }
  if (button == Button::DOWN) {
    thermalSimple_page_cursor_position++;
    if (thermalSimple_page_cursor_position > thermalSimple_page_cursor_max)
      thermalSimple_page_cursor_position = 0;
  }
}

void thermalSimplePage_button(Button button, ButtonState state, uint8_t count) {
  // reset cursor time out count if a button is pushed
  thermalSimple_page_cursor_timeCount = 0;

  switch (thermalSimple_page_cursor_position) {
    case cursor_thermalSimplePage_none:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) thermalSimple_page_cursor_move(button);
          break;
        case Button::RIGHT:
          if (state == RELEASED) {
            display_turnPage(page_next);
            speaker_playSound(fx_increase);
          }
          break;
        case Button::LEFT:
          if (state == RELEASED) {
            display_turnPage(page_prev);
            speaker_playSound(fx_decrease);
          }
          break;
        case Button::CENTER:
          if (state == HELD && count == 2) {
            power_shutdown();
          }
          break;
      }
      break;
    case cursor_thermalSimplePage_alt1:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) thermalSimple_page_cursor_move(button);
          break;
        case Button::LEFT:
          if (NAVPG_ALT_TYP == altType_MSL &&
              (state == PRESSED || state == HELD || state == HELD_LONG)) {
            baro_adjustAltSetting(1, count);
            speaker_playSound(fx_neutral);
          }
          break;
        case Button::RIGHT:
          if (NAVPG_ALT_TYP == altType_MSL &&
              (state == PRESSED || state == HELD || state == HELD_LONG)) {
            baro_adjustAltSetting(-1, count);
            speaker_playSound(fx_neutral);
          }
          break;
        case Button::CENTER:
          if (state == RELEASED)
            settings_adjustDisplayField_thermalPage_alt(Button::CENTER);
          else if (state == HELD && count == 1 && THMPG_ALT_TYP == altType_MSL) {
            if (settings_matchGPSAlt()) {  // successful adjustment of altimeter setting to match
                                           // GPS altitude
              speaker_playSound(fx_enter);
              thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
            } else {  // unsuccessful
              speaker_playSound(fx_cancel);
            }
          }
          break;
      }
      break;
    /* case cursor_thermalSimplePage_alt2:
            switch(button) {
                    case Button::UP:
                            break;
                    case Button::DOWN:
                            break;
                    case Button::LEFT:
                            break;
                    case Button::RIGHT:
                            break;
                    case Button::CENTER:
                            break;
            }
            break;
            */
    case cursor_thermalSimplePage_userField1:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) thermalSimple_page_cursor_move(button);
          break;
        case Button::LEFT:
          break;
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == RELEASED) THMSPG_USR1++;
          if (THMSPG_USR1 >= static_cast<int>(ThermSimpPageUserField1::NONE)) THMSPG_USR1 = 0;
          break;
      }
      break;
      /*
case cursor_thermalSimplePage_userField2:
      switch(button) {
              case Button::UP:
                      break;
              case Button::DOWN:
                      break;
              case Button::LEFT:
                      break;
              case Button::RIGHT:
                      break;
              case Button::CENTER:
                      break;
      }
      break;
      */
    case cursor_thermalSimplePage_timer:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) thermalSimple_page_cursor_move(button);
          break;
        case Button::LEFT:
          break;
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == RELEASED) {
            flightTimer_toggle();
            thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
          } else if (state == HELD) {
            flightTimer_reset();
            thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
          }

          break;
      }
      break;
  }
}