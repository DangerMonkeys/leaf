

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
#include "ui/display/pages/primary/page_basic.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

enum basic_page_items { cursor_basicPage_none, cursor_basicPage_alt1, cursor_basicPage_timer };
uint8_t basic_page_cursor_max = 2;

int8_t basic_page_cursor_position = cursor_basicPage_none;
uint8_t basic_page_cursor_timeCount =
    0;  // count up for every page_draw, and if no button is pressed, then reset cursor to "none"
        // after the timeOut value is reached.
uint8_t basic_page_cursor_timeOut =
    10;  // after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.

void basicPage_reset() {
  basic_page_cursor_position = cursor_basicPage_none;
  basic_page_cursor_timeCount = 0;
}

void basicPage_draw() {
  // if cursor is selecting something, count toward the timeOut value before we reset cursor
  if (basic_page_cursor_position != cursor_basicPage_none &&
      basic_page_cursor_timeCount++ >= basic_page_cursor_timeOut) {
    basic_page_cursor_position = cursor_basicPage_none;
    basic_page_cursor_timeCount = 0;
  }

  u8g2.firstPage();
  do {
    // Draw clock and full footer, but not the full usual header items of the other pages
    u8g2.setFont(leaf_6x10);
    display_clockTime(0, 10, false);
    display_footer(basic_page_cursor_position == cursor_basicPage_timer);

    // Speed
    uint8_t speed_x = 59;
    uint8_t speed_y = 28;
    bool speedIsThreeDigits = false;
    // If don't have a fix, show GPS searching icon; otherwise show speed
    if (!gps.hasUsableFix()) {
      display_GPS_icon(84, 12);
    } else {
      u8g2.setFont(leaf_labels);
      u8g2.setCursor(77, 39);
      if (settings.units_speed)
        u8g2.print("MPH");
      else
        u8g2.print("KPH");

      u8g2.setFont(leaf_21h);
      speedIsThreeDigits = display_speed(speed_x, speed_y);
    }

    // Heading and Wind
    uint8_t center_x = 48;
    uint8_t wind_y = 54;
    uint8_t heading_x = center_x - 12;
    uint8_t heading_y = speed_y;
    if (speedIsThreeDigits) {
      heading_x -= 8;
      if (!settings.units_heading) heading_x -= 3;
    }

    // heading
    u8g2.setFont(leaf_8x14);
    display_heading(heading_x, heading_y, true);

    // wind & compass
    uint8_t wind_radius = 17;
    uint8_t pointer_size = 8;
    bool showPointer = true;
    display_windSockRing(center_x, wind_y, wind_radius, pointer_size, showPointer);

    // Vario Bar
    uint8_t topOfFrame = 18;
    uint8_t varioBarWidth = 12;
    uint8_t varioBarClimbHeight = 85;
    uint8_t varioBarSinkHeight = 70;

    // TODO: display lack of climb rate differently than 0
    int32_t climbRate = baro.climbRateFilteredValid() ? baro.climbRateFiltered() : 0;
    const int32_t displayClimbRate = baro.climbRateFilteredValid() ? baro.climbRateForDisplay() : 0;
    display_varioBar(topOfFrame, varioBarClimbHeight, varioBarSinkHeight, varioBarWidth, climbRate);

    // Climb
    uint8_t climbBoxHeight = 34;
    uint8_t climbBoxY = topOfFrame + varioBarClimbHeight - climbBoxHeight / 2;
    display_climbRatePointerBox(varioBarWidth + 9, climbBoxY, 75, climbBoxHeight,
                                16);  // x, y, w, h, triangle size
    display_climbRate(11, climbBoxY + 31, leaf_28h, displayClimbRate);
    u8g2.setDrawColor(0);
    u8g2.drawPixel(varioBarWidth + 9,
                   climbBoxY);  // one pixel corner needs trimmed due to interaction with triangle
                                // and even-dimension box height
    u8g2.setFont(leaf_28h);
    if (settings.units_climb)
      u8g2.print('f');
    else
      u8g2.print('m');
    u8g2.setDrawColor(1);

    // Altitude
    uint8_t alt_y = 139;
    // Altitude header labels
    u8g2.setFont(leaf_labels);
    u8g2.setCursor(varioBarWidth + 3, alt_y - 1);
    print_alt_label(settings.disp_userPageAltType);
    u8g2.print(" Altitude ");
    // u8g2.setCursor(varioBarWidth + 50, alt_y - 1);
    if (settings.units_alt)
      u8g2.print("ft");
    else
      u8g2.print("m");

    // Alt value
    display_alt_type(varioBarWidth + 1, alt_y + 28, leaf_28h, settings.disp_userPageAltType);

    // if selected, draw the box around it
    if (basic_page_cursor_position == cursor_basicPage_alt1) {
      display_selectionBox(varioBarWidth + 1, alt_y - 2, 96 - (varioBarWidth + 1), 32, 7);
    }

  } while (u8g2.nextPage());
}

void basic_page_cursor_move(Button button) {
  if (button == Button::UP) {
    basic_page_cursor_position--;
    if (basic_page_cursor_position < 0) basic_page_cursor_position = basic_page_cursor_max;
  }
  if (button == Button::DOWN) {
    basic_page_cursor_position++;
    if (basic_page_cursor_position > basic_page_cursor_max) basic_page_cursor_position = 0;
  }
  speaker.playSound(basic_page_cursor_position == cursor_basicPage_none ? fx::doubleClick
                                                                        : fx::click);
}

bool basicPage_volumeShortcut(Button button, ButtonEvent state) {
  if (!settings.volumeShortcut || (button != Button::UP && button != Button::DOWN) ||
      state != ButtonEvent::INCREMENTED) {
    return false;
  }

  if (!settings.adjustShortcutVolume(button)) buttons.consumeButton();
  return true;
}

void basicPage_button(Button button, ButtonEvent state, uint8_t count) {
  // reset cursor time out count if a button is pushed
  basic_page_cursor_timeCount = 0;

  switch (basic_page_cursor_position) {
    case cursor_basicPage_none:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (basicPage_volumeShortcut(button, state)) {
            break;
          } else if (state == ButtonEvent::CLICKED) {
            basic_page_cursor_move(button);
          }
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
    case cursor_basicPage_alt1:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) basic_page_cursor_move(button);
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
            // for now use the same alt field setting on both Basic and User pages
            settings.adjustDisplayField_userPage_alt(Button::CENTER);
          } else if (state == ButtonEvent::INCREMENTED && count == 1 &&
                     settings.disp_userPageAltType == altType_MSL) {
            if (baro.syncToGPSAlt()) {  // successful adjustment of altimeter setting to match
                                        // GPS altitude
              speaker.playSound(fx::enter);
              basic_page_cursor_position = cursor_basicPage_none;
            } else {  // unsuccessful
              speaker.playSound(fx::cancel);
            }
          }
          break;
      }
      break;

    case cursor_basicPage_timer:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) basic_page_cursor_move(button);
          break;
        case Button::LEFT:
          break;
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED && !flightTimer_isRunning()) {
            flightTimer_start();
            basic_page_cursor_position = cursor_basicPage_none;
          } else if (state == ButtonEvent::HELD && flightTimer_isRunning()) {
            buttons.consumeButton();
            flightTimer_stop();
            basic_page_cursor_position = cursor_basicPage_none;
          }

          break;
      }
      break;
  }
  display.update();
}
