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

enum thermal_page_items {
  cursor_thermalPage_none,
  cursor_thermalPage_alt1,
  // cursor_thermalPage_alt2,
  // cursor_thermalPage_userField1,
  // cursor_thermalPage_userField2,
  cursor_thermalPage_timer
};
uint8_t cursor_max = 2;

int8_t cursor_position = cursor_thermalPage_none;
uint8_t cursor_timeCount = 0;  // count up for every page_draw, and if no button is pressed, then
                               // reset cursor to "none" after the timeOut value is reached.
uint8_t cursor_timeOut =
    8;  // after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.

void thermalPage_draw() {
  // if cursor is selecting something, count toward the timeOut value before we reset cursor
  if (cursor_position != cursor_thermalPage_none && cursor_timeCount++ >= cursor_timeOut) {
    cursor_position = cursor_thermalPage_none;
    cursor_timeCount = 0;
  }

  u8g2.firstPage();
  do {
    // draw all status icons, clock, timer, etc (and pass along if timer is selected)
    display_headerAndFooter(cursor_position == cursor_thermalPage_timer, false);

    // Main Info ****************************************************
    // Vario Bar
    uint8_t topOfFrame = 30;
    uint8_t varioBarWidth = 20;
    uint8_t varioBarClimbHeight = 70;
    uint8_t varioBarSinkHeight = varioBarClimbHeight;

    display_varioBar(topOfFrame, varioBarClimbHeight, varioBarSinkHeight, varioBarWidth,
                     baro.climbRateFiltered);

    // Graph Box
    uint8_t graphBoxHeight = 40;
    u8g2.drawFrame(varioBarWidth - 1, topOfFrame, 96 - varioBarWidth + 1, graphBoxHeight);

    // alt
    display_alt_type(22, 89, leaf_8x14, settings.disp_thmPageAltType);

    // altselection box
    if (cursor_position == cursor_thermalPage_alt1) {
      display_selectionBox(21, 73, 96 - 21, 18, 6);
    }

    // climb rate
    display_climbRatePointerBox(20, 92, 76, 17, 6);  // x, y, w, h, triangle size
    display_climbRate(20, 108, leaf_8x14, baro.climbRateFiltered);

    // altitude above launch
    display_altAboveLaunch(24, 132, baro.altAboveLaunch);

    // User Fields ****************************************************
    uint8_t userFieldsTop = 136;
    uint8_t userFieldsHeight = 17;
    uint8_t userFieldsMid = userFieldsTop + userFieldsHeight;
    uint8_t userFieldsBottom = userFieldsMid + userFieldsHeight;
    uint8_t userSecondColumn = varioBarWidth / 2 + 48;

    u8g2.drawHLine(varioBarWidth - 1, userFieldsTop, 96 - varioBarWidth + 1);
    u8g2.drawHLine(varioBarWidth - 1, userFieldsMid, 96 - varioBarWidth + 1);
    u8g2.drawHLine(varioBarWidth - 1, userFieldsBottom, 96 - varioBarWidth + 1);
    u8g2.drawVLine(userSecondColumn, userFieldsTop, userFieldsHeight * 2);

    display_temp(varioBarWidth + 5, userFieldsMid - 1, (int16_t)tempRH_getTemp());
    display_humidity(userSecondColumn + 3, userFieldsMid - 1, (uint8_t)tempRH_getHumidity());
    display_accel(varioBarWidth + 5, userFieldsBottom - 1, IMU_getAccel());
    display_glide(userSecondColumn + 3, userFieldsBottom - 1, gps_getGlideRatio());

    // Footer Info ****************************************************

  } while (u8g2.nextPage());
}

void cursor_move(Button button) {
  if (button == Button::UP) {
    cursor_position--;
    if (cursor_position < 0) cursor_position = cursor_max;
  }
  if (button == Button::DOWN) {
    cursor_position++;
    if (cursor_position > cursor_max) cursor_position = 0;
  }
}

void thermalPage_button(Button button, ButtonState state, uint8_t count) {
  // reset cursor time out count if a button is pushed
  cursor_timeCount = 0;

  switch (cursor_position) {
    case cursor_thermalPage_none:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) cursor_move(button);
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
    case cursor_thermalPage_alt1:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) cursor_move(button);
          break;
        case Button::LEFT:
          if (settings.disp_navPageAltType == altType_MSL &&
              (state == PRESSED || state == HELD || state == HELD_LONG)) {
            baro.adjustAltSetting(1, count);
            speaker_playSound(fx_neutral);
          }
          break;
        case Button::RIGHT:
          if (settings.disp_navPageAltType == altType_MSL &&
              (state == PRESSED || state == HELD || state == HELD_LONG)) {
            baro.adjustAltSetting(-1, count);
            speaker_playSound(fx_neutral);
          }
          break;
        case Button::CENTER:
          if (state == RELEASED)
            settings.adjustDisplayField_thermalPage_alt(Button::CENTER);
          else if (state == HELD && count == 1 && settings.disp_thmPageAltType == altType_MSL) {
            if (baro.syncToGPSAlt()) {  // successful adjustment of altimeter setting to match
                                        // GPS altitude
              speaker_playSound(fx_enter);
              cursor_position = cursor_thermalPage_none;
            } else {  // unsuccessful
              speaker_playSound(fx_cancel);
            }
          }
          break;
      }
      break;
    /* case cursor_thermalPage_alt2:
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
    case cursor_thermalPage_userField1:
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
    case cursor_thermalPage_userField2:
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
    case cursor_thermalPage_timer:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) cursor_move(button);
          break;
        case Button::LEFT:
          break;
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == RELEASED && !flightTimer_isRunning()) {
            flightTimer_start();
            cursor_position = cursor_thermalPage_none;
          } else if (state == HELD && flightTimer_isRunning()) {
            flightTimer_stop();
            cursor_position = cursor_thermalPage_none;
            buttons_lockAfterHold();  // lock buttons so we don't turn off if user keeps holding
                                      // button
          }

          break;
      }
      break;
  }
}