#include "ui/display/pages/primary/page_thermal_adv.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include "hardware/buttons.h"
#include "instruments/ambient.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum thermalAdv_page_items {
  cursor_thermalAdvPage_none,
  cursor_thermalAdvPage_alt1,
  // cursor_thermalAdvPage_alt2,
  // cursor_thermalAdvPage_userField1,
  // cursor_thermalAdvPage_userField2,
  cursor_thermalAdvPage_timer
};
uint8_t cursor_thermalAdvPage_cursor_max = 2;

int8_t thermalAdvPage_cursor_position = cursor_thermalAdvPage_none;
// count up for every page_draw, and if no button is pressed, then reset cursor to "none" after the
// timeOut value is reached.
uint8_t thermalAdvPage_cursor_timeCount = 0;
// after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.
uint8_t cursor_timeOut = 8;

void thermalPageAdv_draw() {
  // if cursor is selecting something, count toward the timeOut value before we reset cursor
  if (thermalAdvPage_cursor_position != cursor_thermalAdvPage_none &&
      thermalAdvPage_cursor_timeCount++ >= cursor_timeOut) {
    thermalAdvPage_cursor_position = cursor_thermalAdvPage_none;
    thermalAdvPage_cursor_timeCount = 0;
  }

  u8g2.firstPage();
  do {
    // draw all status icons, clock, timer, etc (and pass along if timer is selected)
    display_headerAndFooter(thermalAdvPage_cursor_position == cursor_thermalAdvPage_timer, false);

    // Main Info ****************************************************
    // Vario Bar
    uint8_t topOfFrame = 30;
    uint8_t varioBarWidth = 20;
    uint8_t varioBarClimbHeight = 70;
    uint8_t varioBarSinkHeight = varioBarClimbHeight;

    display_varioBar(topOfFrame, varioBarClimbHeight, varioBarSinkHeight, varioBarWidth,
                     baro.climbRateFiltered());

    // Graph Box
    uint8_t graphBoxHeight = 40;
    u8g2.drawFrame(varioBarWidth - 1, topOfFrame, 96 - varioBarWidth + 1, graphBoxHeight);

    // alt
    display_alt_type(22, 89, leaf_8x14, settings.disp_thmPageAltType);

    // altselection box
    if (thermalAdvPage_cursor_position == cursor_thermalAdvPage_alt1) {
      display_selectionBox(21, 73, 96 - 21, 18, 6);
    }

    // climb rate
    display_climbRatePointerBox(20, 92, 76, 17, 6);  // x, y, w, h, triangle size
    display_climbRate(20, 108, leaf_8x14, baro.climbRateFiltered());

    // altitude above launch
    display_altAboveLaunch(24, 132);

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

    display_temp(varioBarWidth + 5, userFieldsMid - 1, ambient);
    display_humidity(userSecondColumn + 3, userFieldsMid - 1, ambient);
    // TODO: show missing accel differently than 0
    float accel = imu.accelValid() ? imu.getAccel() : 0;
    display_accel(varioBarWidth + 5, userFieldsBottom - 1, accel);
    display_glide(userSecondColumn + 3, userFieldsBottom - 1, gps.getGlideRatio());

    // Footer Info ****************************************************

  } while (u8g2.nextPage());
}

void cursor_move(Button button) {
  if (button == Button::UP) {
    thermalAdvPage_cursor_position--;
    if (thermalAdvPage_cursor_position < 0)
      thermalAdvPage_cursor_position = cursor_thermalAdvPage_cursor_max;
  }
  if (button == Button::DOWN) {
    thermalAdvPage_cursor_position++;
    if (thermalAdvPage_cursor_position > cursor_thermalAdvPage_cursor_max)
      thermalAdvPage_cursor_position = 0;
  }
}

void thermalPageAdv_button(Button button, ButtonEvent state, uint8_t count) {
  // reset cursor time out count if a button is pushed
  thermalAdvPage_cursor_timeCount = 0;

  switch (thermalAdvPage_cursor_position) {
    case cursor_thermalAdvPage_none:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) cursor_move(button);
          break;
        case Button::RIGHT:
          if (state == ButtonEvent::CLICKED) {
            display.turnPage(PageAction::Next);
            speaker.playSound(fx::increase);
          }
          break;
        case Button::LEFT:
          if (state == ButtonEvent::CLICKED) {
            display.turnPage(PageAction::Prev);
            speaker.playSound(fx::decrease);
          }
          break;
        case Button::CENTER:
          if (state == ButtonEvent::INCREMENTED && count == 2) {
            power.shutdown();
            return;  // Don't refresh the display; we're shutting down
          }
          break;
      }
      break;
    case cursor_thermalAdvPage_alt1:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) cursor_move(button);
          break;
        case Button::LEFT:
          if (settings.disp_navPageAltType == altType_MSL &&
              (state == ButtonEvent::CLICKED || state == ButtonEvent::INCREMENTED)) {
            baro.adjustAltSetting(1, count);
            speaker.playSound(fx::neutral);
          }
          break;
        case Button::RIGHT:
          if (settings.disp_navPageAltType == altType_MSL &&
              (state == ButtonEvent::CLICKED || state == ButtonEvent::INCREMENTED)) {
            baro.adjustAltSetting(-1, count);
            speaker.playSound(fx::neutral);
          }
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED)
            settings.adjustDisplayField_thermalPage_alt(Button::CENTER);
          else if (state == ButtonEvent::INCREMENTED && count == 1 &&
                   settings.disp_thmPageAltType == altType_MSL) {
            if (baro.syncToGPSAlt()) {  // successful adjustment of altimeter setting to match
                                        // GPS altitude
              speaker.playSound(fx::enter);
              thermalAdvPage_cursor_position = cursor_thermalAdvPage_none;
            } else {  // unsuccessful
              speaker.playSound(fx::cancel);
            }
          }
          break;
      }
      break;
    case cursor_thermalAdvPage_timer:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) cursor_move(button);
          break;
        case Button::LEFT:
          break;
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED && !flightTimer_isRunning()) {
            flightTimer_start();
            thermalAdvPage_cursor_position = cursor_thermalAdvPage_none;
          } else if (state == ButtonEvent::HELD && flightTimer_isRunning()) {
            buttons.consumeButton();
            flightTimer_stop();
            thermalAdvPage_cursor_position = cursor_thermalAdvPage_none;
          }

          break;
      }
      break;
  }
  display.update();
}