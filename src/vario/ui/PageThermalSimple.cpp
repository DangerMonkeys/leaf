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
#include "wind_estimate/wind_estimate.h"

enum thermalSimple_page_items {
  cursor_thermalSimplePage_none,
  cursor_thermalSimplePage_alt1,
  cursor_thermalSimplePage_userField1,
  cursor_thermalSimplePage_userField2,
  cursor_thermalSimplePage_timer
};
uint8_t thermalSimple_page_cursor_max = 4;

int8_t thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
uint8_t thermalSimple_page_cursor_timeCount =
    0;  // count up for every page_draw, and if no button is pressed, then reset cursor to "none"
        // after the timeOut value is reached.
uint8_t thermalSimple_page_cursor_timeOut =
    8;  // after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.

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
    bool showHeadingTurnArrows = false;
    display_headerAndFooter(thermalSimple_page_cursor_position == cursor_thermalSimplePage_timer,
                            showHeadingTurnArrows);

    // wind & compass
    uint8_t center_x = 57;
    uint8_t wind_y = 31;
    uint8_t wind_radius = 12;
    uint8_t pointer_size = 7;
    bool showPointer = true;
    display_windSockRing(center_x, wind_y, wind_radius, pointer_size, showPointer);

    // Main Info ****************************************************

    // Vario Bar
    uint8_t topOfFrame = 21;
    uint8_t varioBarWidth = 25;
    uint8_t varioBarClimbHeight = 75;
    uint8_t varioBarSinkHeight = varioBarClimbHeight;

    display_varioBar(topOfFrame, varioBarClimbHeight, varioBarSinkHeight, varioBarWidth,
                     baro.climbRateFiltered);

    // Altitude
    uint8_t alt_y = 58;
    // Altitude header labels
    u8g2.setFont(leaf_labels);
    u8g2.setCursor(varioBarWidth + 52, alt_y - 1);
    print_alt_label(settings.disp_thmPageAltType);
    u8g2.setCursor(varioBarWidth + 60, alt_y - 9);
    if (settings.units_alt)
      u8g2.print("ft");
    else
      u8g2.print("m");

    // Alt value
    display_alt_type(varioBarWidth + 2, alt_y + 21, leaf_21h, settings.disp_thmPageAltType);

    // if selected, draw the box around it
    if (thermalSimple_page_cursor_position == cursor_thermalSimplePage_alt1) {
      display_selectionBox(varioBarWidth + 1, alt_y - 2, 96 - (varioBarWidth + 1), 25, 7);
    }

    // Climb
    uint8_t climbBoxHeight = 27;
    uint8_t climbBoxY = topOfFrame + varioBarClimbHeight - climbBoxHeight / 2;
    display_climbRatePointerBox(varioBarWidth, climbBoxY, 76, climbBoxHeight,
                                13);  // x, y, w, h, triangle size
    display_climbRate(20, climbBoxY + 24, leaf_21h, baro.climbRateFiltered);
    u8g2.setDrawColor(0);
    u8g2.setFont(leaf_5h);
    u8g2.print(" ");  // put a space, but using a small font so the space isn't too wide
    u8g2.setFont(leaf_21h);
    if (settings.units_climb)
      u8g2.print('f');
    else
      u8g2.print('m');
    u8g2.setDrawColor(1);

    // User Fields
    uint8_t userfield_y = climbBoxY + 53;
    uint8_t userfield_x = varioBarWidth + 4;

    // User Field 1 ****************************************************
    bool isSelected = (thermalSimple_page_cursor_position == cursor_thermalSimplePage_userField1);
    drawUserField(userfield_x, userfield_y, settings.disp_thmPageUser1, isSelected);

    // User Field 2 ****************************************************
    userfield_y += 27;
    isSelected = (thermalSimple_page_cursor_position == cursor_thermalSimplePage_userField2);
    drawUserField(userfield_x, userfield_y, settings.disp_thmPageUser2, isSelected);

    // Footer Info ****************************************************

  } while (u8g2.nextPage());
}

void drawUserField(uint8_t x, uint8_t y, uint8_t field, bool selected) {
  switch (field) {
    case static_cast<int>(ThermSimpPageUserFields::ABOVE_LAUNCH):
      display_altAboveLaunch(x, y, baro.altAboveLaunch);
      break;
    case static_cast<int>(ThermSimpPageUserFields::GLIDE):
      // Glide Ratio
      u8g2.setCursor(x, y - 14);
      u8g2.setFont(leaf_5h);
      u8g2.print("` GLIDE");
      display_glide(x + 20, y, gps_getGlideRatio());
      break;
    case static_cast<int>(ThermSimpPageUserFields::TEMP):
      // Temperature
      u8g2.setCursor(x, y - 14);
      u8g2.setFont(leaf_5h);
      u8g2.print("TEMP");
      display_temp(x + 2, y, (int16_t)tempRH_getTemp());
      // Humidity
      u8g2.setCursor(x + 32, y - 14);
      u8g2.setFont(leaf_5h);
      u8g2.print("HUMID");
      display_humidity(x + 34, y, (uint8_t)tempRH_getHumidity());
      break;
    case static_cast<int>(ThermSimpPageUserFields::ACCEL):
      // Acceleration
      u8g2.setCursor(x, y - 14);
      u8g2.setFont(leaf_5h);
      u8g2.print("ACCEL (G FORCE)");
      display_accel(x + 20, y, IMU_getAccel());
      break;
    case static_cast<int>(ThermSimpPageUserFields::DIST):
      // Distance
      u8g2.setCursor(x, y - 14);
      u8g2.setFont(leaf_5h);
      u8g2.print("DISTANCE FLOWN");
      u8g2.setFont(leaf_6x12);
      display_distance(x + 20, y, logbook.distanceFlown);
      break;
    case static_cast<int>(ThermSimpPageUserFields::AIRSPEED):
      u8g2.setCursor(x, y - 14);
      u8g2.setFont(leaf_5h);
      u8g2.print("APPROX AIRSPEED");
      u8g2.setFont(leaf_6x12);
      u8g2.setCursor(x + 20, y);

      WindEstimate windEstForAirspeed = getWindEstimate();
      // only show airspeed if wind estimate is valid
      if (windEstForAirspeed.validEstimate) {
        float displayAirspeed = windEstForAirspeed.airspeedLive;  // m/s current approx airspeed
        if (settings.units_speed)
          displayAirspeed *= 2.23694f;
        else
          displayAirspeed *= 3.6f;
        if (displayAirspeed < 10) u8g2.print(' ');
        u8g2.print((int)displayAirspeed);
        if (settings.units_speed)
          u8g2.print("mph");
        else
          u8g2.print("kph");
      } else {
        u8g2.setFont(leaf_5x8);
        u8g2.setCursor(u8g2.getCursorX() - 15, u8g2.getCursorY());
        u8g2.print("Wait For Wind...");
      }
      break;
  }

  // if selected, draw the box around it
  if (selected) {
    display_selectionBox(x - 3, y - 23, 96 - (x - 3), 27, 6);
  }
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
          if (settings.disp_navPageAltType == altType_MSL &&
              (state == PRESSED || state == HELD || state == HELD_LONG)) {
            baro.adjustAltSetting(-1, count);
            speaker_playSound(fx_neutral);
          }
          break;
        case Button::RIGHT:
          if (settings.disp_navPageAltType == altType_MSL &&
              (state == PRESSED || state == HELD || state == HELD_LONG)) {
            baro.adjustAltSetting(1, count);
            speaker_playSound(fx_neutral);
          }
          break;
        case Button::CENTER:
          if (state == RELEASED) {
            settings.adjustDisplayField_thermalPage_alt(Button::CENTER);
          } else if (state == HELD && count == 1 && settings.disp_thmPageAltType == altType_MSL) {
            if (baro.syncToGPSAlt()) {  // successful adjustment of altimeter setting to match
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
          if (state == RELEASED) settings.disp_thmPageUser1++;
          if (settings.disp_thmPageUser1 >= static_cast<int>(ThermSimpPageUserFields::NONE))
            settings.disp_thmPageUser1 = 0;
          break;
      }
      break;
    case cursor_thermalSimplePage_userField2:
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
          if (state == RELEASED) settings.disp_thmPageUser2++;
          if (settings.disp_thmPageUser2 >= static_cast<int>(ThermSimpPageUserFields::NONE))
            settings.disp_thmPageUser2 = 0;
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
          if (state == RELEASED && !flightTimer_isRunning()) {
            flightTimer_start();
            thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
          } else if (state == HELD && flightTimer_isRunning()) {
            flightTimer_stop();
            thermalSimple_page_cursor_position = cursor_thermalSimplePage_none;
            buttons_lockAfterHold();  // lock buttons so we don't turn off if user keeps holding
                                      // button
          }

          break;
      }
      break;
  }
}