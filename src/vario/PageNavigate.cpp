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
#include "gpx.h"
#include "log.h"
#include "power.h"
#include "settings.h"
#include "speaker.h"
#include "tempRH.h"

enum navigate_page_items {
  cursor_navigatePage_none,
  cursor_navigatePage_alt1,
  cursor_navigatePage_waypoint,
  // cursor_navigatePage_userField1,
  // cursor_navigatePage_userField2,
  cursor_navigatePage_timer
};
uint8_t navigatePage_cursorMax = 3;

int8_t navigatePage_cursorPosition = cursor_navigatePage_none;
uint8_t navigatePage_cursorTimeCount =
    0;  // count up for every page_draw, and if no button is pressed, then reset cursor to "none"
        // after the timeOut value is reached.
uint8_t navigatePage_cursorTimeOut =
    8;  // after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.

int16_t destination_selection_index = 0;
bool destination_selection_routes_vs_waypoints = true;  // start showing routes not waypoints

void navigatePage_destinationSelect(Button dir) {
  switch (dir) {
    case Button::LEFT:
      destination_selection_index--;
      if (destination_selection_index < 0) {                    // if we wrap off the left end
        if (destination_selection_routes_vs_waypoints) {        // ..and we're showing routes
          if (gpxData.totalWaypoints >= 1) {                    // ..then if we have waypoints
            destination_selection_routes_vs_waypoints = false;  // ..then switch to waypoints
            destination_selection_index =
                gpxData.totalWaypoints;  // ..and set index to the last waypoint (i.e., we're
                                         // scrolling backwards/leftward)
          } else {
            destination_selection_index =
                gpxData.totalRoutes;  // otherwise stay on Routes and show the last route (i.e.,
                                      // wrap around list of routes).  This may just be wrapping
                                      // from 0 back around to 0 if we have no Routes, too.
          }
        } else {  // Or if we're showing Waypoints and wrap off the left end
          if (gpxData.totalRoutes >= 1) {                      // ..then if we have routes
            destination_selection_routes_vs_waypoints = true;  // ..then switch to routes
            destination_selection_index =
                gpxData.totalRoutes;  // ..and set index to the last route (i.e., we're scrolling
                                      // backwards/leftward)
          } else {
            destination_selection_index =
                gpxData.totalWaypoints;  // otherwise stay on waypoints and show the last waypoint
                                         // (i.e., wrap around list of waypoints).  This may just be
                                         // wrapping from 0 back around to 0 if we have no
                                         // waypoints, too.
          }
        }
      }
      break;

    case Button::RIGHT:
      destination_selection_index++;
      if (destination_selection_routes_vs_waypoints) {  // if we're currently showing Routes...
        if (destination_selection_index >
            gpxData.totalRoutes) {            // 		if we're passed the number of Routes we have...
          if (gpxData.totalWaypoints >= 1) {  // 		...and we have waypoints to switch
                                              // to...
            destination_selection_routes_vs_waypoints =
                false;                        //		...switch to waypoints..
            destination_selection_index = 1;  //		...and show the first one
          } else {                            // otherwise loop back to index 0
            destination_selection_index = 0;
          }
        }
      } else {  // otherise if we're showing waypoints...
        if (destination_selection_index >
            gpxData.totalWaypoints) {      //...and we're passed the number of waypoints we have
          if (gpxData.totalRoutes >= 1) {  // 		...and we have routes to switch to...
            destination_selection_routes_vs_waypoints =
                true;                         //		...switch to routes...
            destination_selection_index = 1;  //		...and show the first one
          } else {                            // otherwise loop back to index 0
            destination_selection_index = 0;
          }
        }
      }
      break;
    case Button::CENTER:
      if (destination_selection_routes_vs_waypoints)
        gpx_activateRoute(destination_selection_index);
      else
        gpx_activatePoint(destination_selection_index);
      navigatePage_cursorPosition = cursor_navigatePage_none;
      break;
  }
}

float test_wind_angle_nav = 0;

void navigatePage_draw() {
  // if cursor is selecting something, count toward the timeOut value before we reset cursor
  if (navigatePage_cursorPosition != cursor_navigatePage_none &&
      navigatePage_cursorTimeCount++ >= navigatePage_cursorTimeOut) {
    navigatePage_cursorPosition = cursor_navigatePage_none;
    navigatePage_cursorTimeCount = 0;
  }

  u8g2.firstPage();
  do {
    // draw all status icons, clock, timer, etc (and pass along if timer is selected)
    display_headerAndFooter(true, (navigatePage_cursorPosition == cursor_navigatePage_timer));

    ///////////////////////////////////////////////////
    // Nav Circle

    // Nav Circles Locations
    uint8_t nav_x = 57;
    uint8_t nav_y = 52;
    uint8_t nav_r = 37;
    uint8_t wind_r = 8;

    u8g2.drawCircle(nav_x, nav_y, nav_r);
    u8g2.drawCircle(nav_x, nav_y, nav_r + 1);

    // Straight Arrow Pointer (Travel Direction)
    uint8_t pointer_w = 5;  // half width of arrowhead
    uint8_t pointer_h = 9;  // full height of arrowhead
    uint8_t pointer_x = nav_x;
    uint8_t pointer_y = nav_y - nav_r - 2;  // tip of arrow

    u8g2.setDrawColor(0);
    u8g2.drawBox(nav_x - (pointer_w) / 2, nav_y - nav_r, pointer_w, 2);
    u8g2.setDrawColor(1);
    // arrow point
    u8g2.drawLine(pointer_x - pointer_w, pointer_y + pointer_h, pointer_x, pointer_y);
    u8g2.drawLine(pointer_x + pointer_w, pointer_y + pointer_h, pointer_x, pointer_y);
    u8g2.drawLine(pointer_x - pointer_w - 1, pointer_y + pointer_h, pointer_x - 1, pointer_y);
    u8g2.drawLine(pointer_x + pointer_w + 1, pointer_y + pointer_h, pointer_x + 1, pointer_y);
    // arrow flats
    u8g2.drawLine(pointer_x - pointer_w,
                  pointer_y + pointer_h,
                  pointer_x - pointer_w / 2,
                  pointer_y + pointer_h);
    u8g2.drawLine(pointer_x + pointer_w,
                  pointer_y + pointer_h,
                  pointer_x + pointer_w / 2,
                  pointer_y + pointer_h);
    // arrow shaft
    u8g2.drawLine(pointer_x - pointer_w / 2,
                  pointer_y + pointer_h,
                  pointer_x - pointer_w / 2,
                  pointer_y + pointer_h * 2);
    u8g2.drawLine(pointer_x + pointer_w / 2,
                  pointer_y + pointer_h,
                  pointer_x + pointer_w / 2,
                  pointer_y + pointer_h * 2);

    // Waypoint Pointer
    if (gpxNav.navigating) {
      uint8_t waypoint_tip_r = nav_r - 3;
      uint8_t waypoint_shaft_r = waypoint_tip_r - 3;
      uint8_t waypoint_tail_r = waypoint_tip_r - 5;
      float waypoint_arrow_angle = 0.15;  // 0.205;

      float directionToWaypoint = gpxNav.turnToActive * DEG_TO_RAD;

      int8_t waypoint_tip_x = sin(directionToWaypoint) * waypoint_tip_r + nav_x;
      int8_t waypoint_tip_y = nav_y - cos(directionToWaypoint) * waypoint_tip_r;
      int8_t waypoint_shaft_x = sin(directionToWaypoint) * waypoint_shaft_r + nav_x;
      int8_t waypoint_shaft_y = nav_y - cos(directionToWaypoint) * waypoint_shaft_r;

      u8g2.drawLine(nav_x + 1, nav_y, waypoint_shaft_x + 1, waypoint_shaft_y);
      u8g2.drawLine(nav_x, nav_y + 1, waypoint_shaft_x, waypoint_shaft_y + 1);
      u8g2.drawLine(nav_x,
                    nav_y,
                    waypoint_shaft_x,
                    waypoint_shaft_y);  // the real center line; others are just to fatten it up
      u8g2.drawLine(nav_x - 1, nav_y, waypoint_shaft_x - 1, waypoint_shaft_y);
      u8g2.drawLine(nav_x, nav_y - 1, waypoint_shaft_x, waypoint_shaft_y - 1);

      int8_t tail_left_x =
          sin(directionToWaypoint - waypoint_arrow_angle) * (waypoint_tail_r) + nav_x;
      int8_t tail_left_y =
          nav_y - cos(directionToWaypoint - waypoint_arrow_angle) * (waypoint_tail_r);
      int8_t tail_right_x =
          sin(directionToWaypoint + waypoint_arrow_angle) * (waypoint_tail_r) + nav_x;
      int8_t tail_right_y =
          nav_y - cos(directionToWaypoint + waypoint_arrow_angle) * (waypoint_tail_r);

      u8g2.drawLine(tail_left_x, tail_left_y, waypoint_tip_x, waypoint_tip_y);
      u8g2.drawLine(tail_right_x, tail_right_y, waypoint_tip_x, waypoint_tip_y);
      u8g2.drawLine(tail_right_x, tail_right_y, tail_left_x, tail_left_y);
      u8g2.drawTriangle(
          tail_left_x, tail_left_y, waypoint_tip_x, waypoint_tip_y, tail_right_x, tail_right_y);
    }

    // Wind sock
    u8g2.drawDisc(nav_x, nav_y, wind_r + 2);
    u8g2.setDrawColor(0);
    display_windSock(nav_x, nav_y, wind_r, test_wind_angle_nav);  // 0.78);
    u8g2.setDrawColor(1);

    test_wind_angle_nav += .1;
    if (test_wind_angle_nav > 2 * PI) test_wind_angle_nav -= (2 * PI);

    ///////////////////////////////////////////////////
    // Vario Info *************************************
    uint8_t topOfFrame = 27;
    uint8_t graphBoxHeight = 40;
    uint8_t varioBarWidth = 20;
    uint8_t varioBarHeight = 101;
    uint8_t varioBoxHeight = 18;

    // blank out the bottom bit of the nav circle (to make room for climb rate and altitude and
    // other fields etc)
    u8g2.setDrawColor(0);
    u8g2.drawBox(varioBarWidth, topOfFrame + varioBarHeight / 2 + varioBoxHeight / 2, 76, 5);
    u8g2.setDrawColor(1);

    // Vario Bar
    display_varioBar(topOfFrame, varioBarHeight, varioBarWidth, baro.climbRateFiltered);

    // climb
    display_climbRatePointerBox(varioBarWidth,
                                topOfFrame + varioBarHeight / 2 - varioBoxHeight / 2,
                                96 - varioBarWidth,
                                varioBoxHeight,
                                6);  // x, y, w, h, triangle size
    display_climbRate(varioBarWidth,
                      topOfFrame + varioBarHeight / 2 - varioBoxHeight / 2 + 16,
                      leaf_8x14,
                      baro.climbRateFiltered);

    // alt
    display_alt_type(22, 104, leaf_8x14, NAVPG_ALT_TYP);
    // alt label
    uint8_t label_y = u8g2.getCursorY();
    u8g2.setCursor(77, label_y + 2);
    print_alt_label(NAVPG_ALT_TYP);
    u8g2.setCursor(85, label_y - 6);
    if (UNITS_alt)
      u8g2.print("ft");
    else
      u8g2.print("m");

    // selection box
    if (navigatePage_cursorPosition == cursor_navigatePage_alt1) {
      display_selectionBox(21, 88, 96 - 21, 18, 5);
    }
    // display_altAboveLaunch(24, 129, baro.altAboveLaunch);

    ///////////////////////////////////////////////////
    // Waypoint Info **********************************
    // Name
    u8g2.setCursor(varioBarWidth + 2, topOfFrame + varioBarHeight - 6);
    u8g2.setFont(u8g2_font_12x6LED_tf);

    String defaultWaypointString = "<Select Dest>";
    if (gpxData.totalRoutes <= 0 && gpxData.totalWaypoints <= 0) {
      defaultWaypointString = "No Waypoints!";
    }

    // if the cursor is here, write the waypoint/route name of where the selection index is, rather
    // than the active waypoint).
    if (navigatePage_cursorPosition == cursor_navigatePage_waypoint) {
      u8g2.setCursor(u8g2.getCursorX() + 6,
                     u8g2.getCursorY());  // Also scoot the string over a few pixels to make room
                                          // for selection box
      if (destination_selection_index == 0) {
        u8g2.print(defaultWaypointString);
      } else {
        if (destination_selection_routes_vs_waypoints) {
          u8g2.setFont(leaf_icons);
          u8g2.print('R');
          u8g2.setFont(u8g2_font_12x6LED_tf);
          u8g2.print(gpxData.routes[destination_selection_index].name);
        } else {
          u8g2.print(gpxData.waypoints[destination_selection_index].name);
        }
      }
    } else {
      if (gpxNav.navigating)
        u8g2.print(gpxNav.activePoint.name.c_str());
      else
        u8g2.print(defaultWaypointString);
      u8g2.setFont(leaf_6x12);
    }

    // selection box
    if (navigatePage_cursorPosition == cursor_navigatePage_waypoint) {
      display_selectionBox(
          varioBarWidth + 1, topOfFrame + varioBarHeight - 21, 96 - varioBarWidth - 1, 19, 5);
    }

    // Progress Bar
    float percent_progress;
    if (gpxNav.navigating) {
      percent_progress = 1 - gpxNav.pointDistanceRemaining / gpxNav.segmentDistance;
      if (percent_progress < 0) percent_progress = 0;
    } else {
      percent_progress = 0;
    }
    u8g2.drawFrame(0, topOfFrame + varioBarHeight - 1, 96, 4);
    u8g2.drawBox(0, topOfFrame + varioBarHeight, percent_progress * 96, 2);

    // User Fields ****************************************************

    // Layout
    uint8_t userFieldsTop = topOfFrame + varioBarHeight + 3;
    uint8_t userFieldsHeight = 21;
    uint8_t userFieldsMid = userFieldsTop + userFieldsHeight;
    uint8_t userFieldsBottom = userFieldsMid + userFieldsHeight;
    uint8_t userSecondColumn = 48;

    // u8g2.drawHLine(varioBarWidth-1, userFieldsTop, 96-varioBarWidth+1);
    // u8g2.drawHLine(0, userFieldsMid, 96);
    // u8g2.drawHLine(0, userFieldsBottom, 96);
    // u8g2.drawVLine(userSecondColumn, userFieldsTop, userFieldsHeight*2);

    //  1 | 2
    // ---|---
    //  3 | 4

    // User Field 1 -- Time to waypoint
    display_waypointTimeRemaining(5, userFieldsMid - 1, leaf_6x12);
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(0, userFieldsMid - 14);
    u8g2.print("TIME>&");
    u8g2.setFont(leaf_6x12);

    // User Field 2  -- Dist to waypoint
    display_distance(userSecondColumn + 4, userFieldsMid - 1, gpxNav.pointDistanceRemaining);
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(userSecondColumn + 2, userFieldsMid - 14);
    u8g2.print("DIST>&");
    u8g2.setFont(leaf_6x12);

    // User Field 3 -- Glide Ratio
    display_glide(5, userFieldsBottom - 1, gps_getGlideRatio());
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(0, userFieldsBottom - 14);
    u8g2.print("GLIDE`");
    u8g2.setFont(leaf_6x12);

    // User Field 4	-- Glide Ratio to Waypoint
    display_glide(userSecondColumn + 4, userFieldsBottom - 1, gpxNav.glideToActive);
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(userSecondColumn + 2, userFieldsBottom - 14);
    u8g2.print("`>&");
    u8g2.setFont(leaf_6x12);

    // Footer Info ****************************************************

  } while (u8g2.nextPage());
}

void nav_cursor_move(Button button) {
  if (button == Button::UP) {
    navigatePage_cursorPosition--;
    if (navigatePage_cursorPosition < 0) navigatePage_cursorPosition = navigatePage_cursorMax;
  }
  if (button == Button::DOWN) {
    navigatePage_cursorPosition++;
    if (navigatePage_cursorPosition > navigatePage_cursorMax) navigatePage_cursorPosition = 0;
  }

  if (navigatePage_cursorPosition == cursor_navigatePage_waypoint) {
    if (gpxNav.navigating) {
      if (gpxNav.activeRouteIndex) {
        destination_selection_index = gpxNav.activeRouteIndex;
        destination_selection_routes_vs_waypoints = true;
      } else if (gpxNav.activePointIndex) {
        destination_selection_index = gpxNav.activePointIndex;
        destination_selection_routes_vs_waypoints = false;
      }
    }
  }
}

void navigatePage_button(Button button, ButtonState state, uint8_t count) {
  // reset cursor time out count if a button is pushed
  navigatePage_cursorTimeCount = 0;

  switch (navigatePage_cursorPosition) {
    case cursor_navigatePage_none:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) nav_cursor_move(button);
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
    case cursor_navigatePage_alt1:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) nav_cursor_move(button);
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
            settings_adjustDisplayField_navPage_alt(Button::CENTER);
          else if (state == HELD && count == 1 && NAVPG_ALT_TYP == altType_MSL) {
            if (settings_matchGPSAlt()) {  // successful adjustment of altimeter setting to match
                                           // GPS altitude
              speaker_playSound(fx_enter);
              navigatePage_cursorPosition = cursor_navigatePage_none;
            } else {  // unsuccessful
              speaker_playSound(fx_cancel);
            }
          }
          break;
      }
      break;
    case cursor_navigatePage_waypoint:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) nav_cursor_move(button);
          break;
        case Button::LEFT:
          if (state == RELEASED) navigatePage_destinationSelect(Button::LEFT);
          break;
        case Button::RIGHT:
          if (state == RELEASED) navigatePage_destinationSelect(Button::RIGHT);
          break;
        case Button::CENTER:
          if (state == RELEASED) navigatePage_destinationSelect(Button::CENTER);
          if (state == HELD) {
            gpx_cancelNav();
            navigatePage_cursorPosition = cursor_navigatePage_none;
          }
          break;
      }
      break;
    /*
    case cursor_navigatePage_userField1:
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
    case cursor_navigatePage_userField2:
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
    case cursor_navigatePage_timer:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == RELEASED) nav_cursor_move(button);
          break;
        case Button::LEFT:
          break;
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == RELEASED) {
            flightTimer_toggle();
            navigatePage_cursorPosition = cursor_navigatePage_none;
          } else if (state == HELD) {
            flightTimer_reset();
            navigatePage_cursorPosition = cursor_navigatePage_none;
          }

          break;
      }
      break;
  }
}