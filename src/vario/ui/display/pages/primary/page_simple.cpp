

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
#include "ui/display/pages/primary/page_simple.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

enum simple_page_items { cursor_simplePage_none, cursor_simplePage_alt1, cursor_simplePage_timer };
uint8_t simple_page_cursor_max = 2;

int8_t simple_page_cursor_position = cursor_simplePage_none;
uint8_t simple_page_cursor_timeCount =
    0;  // count up for every page_draw, and if no button is pressed, then reset cursor to "none"
        // after the timeOut value is reached.
uint8_t simple_page_cursor_timeOut =
    8;  // after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.

void simplePage_draw() {
  // if cursor is selecting something, count toward the timeOut value before we reset cursor
  if (simple_page_cursor_position != cursor_simplePage_none &&
      simple_page_cursor_timeCount++ >= simple_page_cursor_timeOut) {
    simple_page_cursor_position = cursor_simplePage_none;
    simple_page_cursor_timeCount = 0;
  }

  u8g2.firstPage();
  do {
    // draw all status icons, clock, timer, etc (and pass along if timer is selected)
    bool showHeadingTurnArrows = false;
    display_headerAndFooter(simple_page_cursor_position == cursor_simplePage_timer,
                            showHeadingTurnArrows);

    /*
    // wind & compass
    uint8_t center_x = 57;
    uint8_t wind_y = 31;
    uint8_t wind_radius = 12;
    uint8_t pointer_size = 7;
    bool showPointer = true;
    display_windSockRing(center_x, wind_y, wind_radius, pointer_size, showPointer);
    */

    // Main Info ****************************************************

    // Vario Bar
    uint8_t topOfFrame = 21;
    uint8_t varioBarWidth = 12;
    uint8_t varioBarClimbHeight = 75;
    uint8_t varioBarSinkHeight = varioBarClimbHeight;

    // TODO: display lack of climb rate differently than 0
    int32_t climbRate = baro.climbRateFilteredValid() ? baro.climbRateFiltered() : 0;
    display_varioBar(topOfFrame, varioBarClimbHeight, varioBarSinkHeight, varioBarWidth, climbRate);

    // Climb
    uint8_t climbBoxHeight = 34;
    uint8_t climbBoxY = topOfFrame + varioBarClimbHeight - climbBoxHeight / 2;
    display_climbRatePointerBox(varioBarWidth + 9, climbBoxY, 75, climbBoxHeight,
                                16);  // x, y, w, h, triangle size
    display_climbRate(11, climbBoxY + 31, leaf_28h, climbRate);
    u8g2.setDrawColor(0);
    u8g2.setFont(leaf_28h);
    if (settings.units_climb)
      u8g2.print('f');
    else
      u8g2.print('m');
    u8g2.setDrawColor(1);

    // Altitude
    uint8_t alt_y = 140;
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
    display_alt_type(varioBarWidth + 2, alt_y + 29, leaf_28h, settings.disp_thmPageAltType);

    // if selected, draw the box around it
    if (simple_page_cursor_position == cursor_simplePage_alt1) {
      display_selectionBox(varioBarWidth + 1, alt_y - 2, 96 - (varioBarWidth + 1), 25, 7);
    }

  } while (u8g2.nextPage());
}

void simple_page_cursor_move(Button button) {
  if (button == Button::UP) {
    simple_page_cursor_position--;
    if (simple_page_cursor_position < 0) simple_page_cursor_position = simple_page_cursor_max;
  }
  if (button == Button::DOWN) {
    simple_page_cursor_position++;
    if (simple_page_cursor_position > simple_page_cursor_max) simple_page_cursor_position = 0;
  }
}

void simplePage_button(Button button, ButtonEvent state, uint8_t count) {
  // reset cursor time out count if a button is pushed
  simple_page_cursor_timeCount = 0;

  switch (simple_page_cursor_position) {
    case cursor_simplePage_none:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) simple_page_cursor_move(button);
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
    case cursor_simplePage_alt1:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) simple_page_cursor_move(button);
          break;
        case Button::LEFT:
          if (settings.disp_navPageAltType == altType_MSL &&
              (state == ButtonEvent::CLICKED || state == ButtonEvent::INCREMENTED)) {
            baro.adjustAltSetting(-1, count);
            speaker.playSound(fx::neutral);
          }
          break;
        case Button::RIGHT:
          if (settings.disp_navPageAltType == altType_MSL &&
              (state == ButtonEvent::CLICKED || state == ButtonEvent::INCREMENTED)) {
            baro.adjustAltSetting(1, count);
            speaker.playSound(fx::neutral);
          }
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED) {
            // for now use the same alt field setting on both simple & thermal pages
            settings.adjustDisplayField_thermalPage_alt(Button::CENTER);
          } else if (state == ButtonEvent::INCREMENTED && count == 1 &&
                     settings.disp_thmPageAltType == altType_MSL) {
            if (baro.syncToGPSAlt()) {  // successful adjustment of altimeter setting to match
                                        // GPS altitude
              speaker.playSound(fx::enter);
              simple_page_cursor_position = cursor_simplePage_none;
            } else {  // unsuccessful
              speaker.playSound(fx::cancel);
            }
          }
          break;
      }
      break;

    case cursor_simplePage_timer:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) simple_page_cursor_move(button);
          break;
        case Button::LEFT:
          break;
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED && !flightTimer_isRunning()) {
            flightTimer_start();
            simple_page_cursor_position = cursor_simplePage_none;
          } else if (state == ButtonEvent::HELD && flightTimer_isRunning()) {
            buttons.consumeButton();
            flightTimer_stop();
            simple_page_cursor_position = cursor_simplePage_none;
          }

          break;
      }
      break;
  }
  display.update();
}