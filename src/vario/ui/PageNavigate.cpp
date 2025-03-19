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
#include "navigation/nav_ids.h"
#include "power.h"
#include "settings.h"
#include "speaker.h"
#include "tempRH.h"
#include "wind_estimate/wind_estimate.h"

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
          if (navigator.totalWaypoints >= 1) {                  // ..then if we have waypoints
            destination_selection_routes_vs_waypoints = false;  // ..then switch to waypoints
            destination_selection_index =
                navigator.totalWaypoints;  // ..and set index to the last waypoint (i.e., we're
                                           // scrolling backwards/leftward)
          } else {
            destination_selection_index =
                navigator.totalRoutes;  // otherwise stay on Routes and show the last route (i.e.,
                                        // wrap around list of routes).  This may just be wrapping
                                        // from 0 back around to 0 if we have no Routes, too.
          }
        } else {  // Or if we're showing Waypoints and wrap off the left end
          if (navigator.totalRoutes >= 1) {                    // ..then if we have routes
            destination_selection_routes_vs_waypoints = true;  // ..then switch to routes
            destination_selection_index =
                navigator.totalRoutes;  // ..and set index to the last route (i.e., we're
                                        // scrolling backwards/leftward)
          } else {
            destination_selection_index =
                navigator.totalWaypoints;  // otherwise stay on waypoints and show the last
                                           // waypoint (i.e., wrap around list of waypoints).
                                           // This may just be wrapping from 0 back around to 0
                                           // if we have no waypoints, too.
          }
        }
      }
      break;

    case Button::RIGHT:
      destination_selection_index++;
      if (destination_selection_routes_vs_waypoints) {  // if we're currently showing Routes...
        if (destination_selection_index >
            navigator.totalRoutes) {            // 		if we're passed the number of Routes we have...
          if (navigator.totalWaypoints >= 1) {  // 		...and we have waypoints to switch
                                                // to...
            destination_selection_routes_vs_waypoints = false;  //		...switch to waypoints..
            destination_selection_index = 1;                    //		...and show the first one
          } else {                                              // otherwise loop back to index 0
            destination_selection_index = 0;
          }
        }
      } else {  // otherise if we're showing waypoints...
        if (destination_selection_index >
            navigator.totalWaypoints) {      //...and we're passed the number of waypoints we have
          if (navigator.totalRoutes >= 1) {  // 		...and we have routes to switch to...
            destination_selection_routes_vs_waypoints = true;  //		...switch to routes...
            destination_selection_index = 1;                   //		...and show the first one
          } else {                                             // otherwise loop back to index 0
            destination_selection_index = 0;
          }
        }
      }
      break;
    case Button::CENTER:
      if (destination_selection_routes_vs_waypoints)
        navigator.activateRoute(RouteID(destination_selection_index));
      else
        navigator.activatePoint(WaypointID(destination_selection_index));
      navigatePage_cursorPosition = cursor_navigatePage_none;
      break;
  }
}

void navigatePage_draw() {
  // if cursor is selecting something, count toward the timeOut value before we reset cursor
  if (navigatePage_cursorPosition != cursor_navigatePage_none &&
      navigatePage_cursorTimeCount++ >= navigatePage_cursorTimeOut) {
    navigatePage_cursorPosition = cursor_navigatePage_none;
    navigatePage_cursorTimeCount = 0;
  }

  u8g2.firstPage();
  do {
    ///////////////////////////////
    // Main Layout Definitions

    // Nav Circles Locations
    uint8_t nav_x = 56;
    uint8_t nav_y = 52;
    uint8_t nav_r = 38;

    // Vario Bar
    uint8_t topOfFrame = 13;
    uint8_t varioBarWidth = 18;
    uint8_t varioBarTopHeight = 75;
    uint8_t varioBarBottomHeight = 45;
    uint8_t varioBarHeight = varioBarTopHeight + varioBarBottomHeight + 1;
    uint8_t varioBarMidpoint = topOfFrame + varioBarTopHeight;
    uint8_t varioBoxHeight = 18;

    //////////////////////
    // Draw Nav Box/Circle  -- do this first so then we can draw the header info on top of it
    u8g2.setDrawColor(1);
    u8g2.drawBox(varioBarWidth, topOfFrame, 96 - varioBarWidth, nav_r * 2 + 4);
    u8g2.setDrawColor(0);
    u8g2.drawDisc(nav_x, nav_y, nav_r);
    u8g2.setDrawColor(1);

    // notch out the box for speed digits
    u8g2.setDrawColor(0);
    u8g2.drawBox(68, topOfFrame, 96 - 68, 2);
    u8g2.drawHLine(64, topOfFrame, 4);

    ///////////////////
    // Draw Header Info
    bool showHeadingTurnArrows = true;
    display_headerAndFooter(navigatePage_cursorPosition == cursor_navigatePage_timer,
                            showHeadingTurnArrows);

    // Arrow Point - Direction of Travel
    uint8_t pointer_w = 5;  // half width of arrowhead
    uint8_t pointer_h = 9;  // full height of arrowhead
    uint8_t shaft_h = 6;    // height of arrow shaft
    uint8_t pointer_x = nav_x;
    uint8_t pointer_y = nav_y - nav_r - 3;  // tip of arrow

    // erase top of circle so pointer arrow can poke through
    u8g2.setDrawColor(0);
    u8g2.drawBox(nav_x - pointer_w / 2, nav_y - nav_r - 2, pointer_w, 4);
    u8g2.setDrawColor(1);
    // arrow point
    u8g2.drawLine(pointer_x - pointer_w, pointer_y + pointer_h, pointer_x, pointer_y);
    u8g2.drawLine(pointer_x + pointer_w, pointer_y + pointer_h, pointer_x, pointer_y);
    u8g2.drawLine(pointer_x - pointer_w - 1, pointer_y + pointer_h, pointer_x - 1, pointer_y);
    u8g2.drawLine(pointer_x + pointer_w + 1, pointer_y + pointer_h, pointer_x + 1, pointer_y);
    // arrow flats
    u8g2.drawLine(pointer_x - pointer_w, pointer_y + pointer_h, pointer_x - pointer_w / 2,
                  pointer_y + pointer_h);
    u8g2.drawLine(pointer_x + pointer_w, pointer_y + pointer_h, pointer_x + pointer_w / 2,
                  pointer_y + pointer_h);
    // arrow shaft
    u8g2.drawLine(pointer_x - pointer_w / 2, pointer_y + pointer_h, pointer_x - pointer_w / 2,
                  pointer_y + pointer_h + shaft_h);
    u8g2.drawLine(pointer_x + pointer_w / 2, pointer_y + pointer_h, pointer_x + pointer_w / 2,
                  pointer_y + pointer_h + shaft_h);

    // Waypoint Drop Shape Placemark Icon
    // just testing options here:
    // displayWaypointDropletPointer(nav_x, nav_y, nav_r, gpxNav.turnToActive * DEG_TO_RAD + PI /
    // 2);

    // Waypoint Pointer
    if (navigator.navigating) {
      uint8_t waypoint_tip_r = nav_r - 1;
      uint8_t waypoint_shaft_r = waypoint_tip_r - 3;
      uint8_t waypoint_shaft_length = 9;
      uint8_t waypoint_tail_r = waypoint_tip_r - 5;
      float waypoint_arrow_angle = 0.15;  // 0.205;

      float directionToWaypoint = navigator.turnToActive * DEG_TO_RAD;

      int8_t waypoint_tip_x = nav_x + sin(directionToWaypoint) * waypoint_tip_r;
      int8_t waypoint_tip_y = nav_y - cos(directionToWaypoint) * waypoint_tip_r;
      int8_t waypoint_shaft_x = nav_x + sin(directionToWaypoint) * waypoint_shaft_r;
      int8_t waypoint_shaft_y = nav_y - cos(directionToWaypoint) * waypoint_shaft_r;
      int8_t waypoint_root_x =
          nav_x + sin(directionToWaypoint) * (waypoint_shaft_r - waypoint_shaft_length);
      int8_t waypoint_root_y =
          nav_y - cos(directionToWaypoint) * (waypoint_shaft_r - waypoint_shaft_length);

      // draw the line plus 4 others slightly offset, to "fatten" the line a bit
      u8g2.drawLine(waypoint_root_x + 1, waypoint_root_y, waypoint_shaft_x + 1, waypoint_shaft_y);
      u8g2.drawLine(waypoint_root_x, waypoint_root_y + 1, waypoint_shaft_x, waypoint_shaft_y + 1);
      u8g2.drawLine(waypoint_root_x, waypoint_root_y, waypoint_shaft_x,
                    waypoint_shaft_y);  // the center;
      u8g2.drawLine(waypoint_root_x - 1, waypoint_root_y, waypoint_shaft_x - 1, waypoint_shaft_y);
      u8g2.drawLine(waypoint_root_x, waypoint_root_y - 1, waypoint_shaft_x, waypoint_shaft_y - 1);

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
      u8g2.drawTriangle(tail_left_x, tail_left_y, waypoint_tip_x, waypoint_tip_y, tail_right_x,
                        tail_right_y);
    }
    // wind & compass
    uint8_t wind_radius = 12;
    uint8_t pointer_size = 7;
    bool showPointer = false;
    display_windSockRing(nav_x, nav_y, wind_radius, pointer_size, showPointer);

    ///////////////////////////////////////////////////
    // Vario Info *************************************

    // Vario Bar
    display_varioBar(topOfFrame, varioBarTopHeight, varioBarBottomHeight, varioBarWidth,
                     baro.climbRateFiltered);

    // Climb
    // Climb Triangle
    uint8_t climbTriangleWidth = 9;
    uint8_t triLeftX = varioBarWidth - climbTriangleWidth - 1;
    uint8_t triLeftY = topOfFrame + varioBarTopHeight;
    uint8_t triRightX = varioBarWidth - 1;
    uint8_t triTopY = topOfFrame + varioBarTopHeight - climbTriangleWidth;
    uint8_t triBotY = topOfFrame + varioBarTopHeight + climbTriangleWidth;
    u8g2.drawTriangle(triLeftX, triLeftY,   // left
                      triRightX, triTopY,   // top
                      triRightX, triBotY);  // bottom
    // now erase a gap between the triangle and the fill of the vario bar
    u8g2.setDrawColor(0);
    u8g2.drawLine(triLeftX - 1, triLeftY, triRightX - 1, triTopY);
    u8g2.drawLine(triLeftX - 1, triLeftY - 1, triRightX - 1, triTopY - 1);
    u8g2.drawLine(triLeftX - 1, triLeftY, triRightX - 1, triBotY);
    u8g2.drawLine(triLeftX - 1, triLeftY + 1, triRightX - 1, triBotY + 1);
    u8g2.setDrawColor(1);

    // climb background
    u8g2.drawBox(varioBarWidth, topOfFrame + 2 * nav_r + 4, 30, 17);

    // climb units
    u8g2.setFont(leaf_labels);
    u8g2.setDrawColor(0);
    u8g2.setFontMode(1);
    if (settings.units_climb) {  // FPM units
      u8g2.setCursor(varioBarWidth, varioBarMidpoint - 5);
      u8g2.print('f');
      u8g2.setCursor(u8g2.getCursorX() - 1, u8g2.getCursorY() + 3);
      u8g2.print('p');
      u8g2.setCursor(u8g2.getCursorX() - 1, u8g2.getCursorY() + 5);
      u8g2.print('m');
    } else {  // M/S units
      u8g2.setCursor(varioBarWidth + 1, varioBarMidpoint - 5);
      u8g2.print('m');
      uint8_t lineX1 = u8g2.getCursorX() - 5;
      uint8_t lineY1 = u8g2.getCursorY() + 3;
      uint8_t lineX2 = lineX1 + 5;
      uint8_t lineY2 = lineY1 - 5;
      u8g2.drawLine(lineX1, lineY1, lineX2, lineY2);
      u8g2.drawLine(lineX1 + 1, lineY1, lineX2 + 1, lineY2);
      u8g2.setCursor(u8g2.getCursorX(), u8g2.getCursorY() + 8);
      u8g2.print('s');
    }
    u8g2.setFontMode(0);

    // climb sign
    u8g2.setCursor(varioBarWidth - 6, varioBarMidpoint + 6);
    u8g2.setFont(leaf_8x14);
    u8g2.setDrawColor(0);
    if (baro.climbRateFiltered >= 0)
      u8g2.print("+");
    else
      u8g2.print("-");

    // climb value
    display_unsignedClimbRate_short(varioBarWidth + 1, varioBarMidpoint + 20,
                                    baro.climbRateFiltered);

    // alt
    display_alt_type(49, 109, leaf_8x14, settings.disp_navPageAltType);
    // alt label
    u8g2.setDrawColor(0);
    u8g2.setCursor(78, topOfFrame + 2 * nav_r + 5);
    print_alt_label(settings.disp_navPageAltType);
    u8g2.setCursor(86, topOfFrame + 2 * nav_r - 3);
    if (settings.units_alt)
      u8g2.print("ft");
    else
      u8g2.print("m");
    u8g2.setDrawColor(1);

    // selection box
    if (navigatePage_cursorPosition == cursor_navigatePage_alt1) {
      display_selectionBox(49, 94, 96 - 49, 17, 5);
    }
    // display_altAboveLaunch(24, 129, baro.altAboveLaunch);

    ///////////////////////////////////////////////////
    // Waypoint Info **********************************
    // Waypoint Name
    uint8_t waypointNameY = topOfFrame + varioBarHeight - 9;
    u8g2.setCursor(varioBarWidth + 2, waypointNameY);
    u8g2.setFont(u8g2_font_12x6LED_tf);

    String defaultWaypointString = "<Select Dest>";
    if (navigator.totalRoutes <= 0 && navigator.totalWaypoints <= 0) {
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
          u8g2.print(navigator.routes[destination_selection_index].name);
        } else {
          u8g2.print(navigator.waypoints[destination_selection_index].name);
        }
      }
    } else {
      if (navigator.navigating)
        u8g2.print(navigator.activePoint.name.c_str());
      else
        u8g2.print(defaultWaypointString);
      u8g2.setFont(leaf_6x12);
    }

    // selection box
    if (navigatePage_cursorPosition == cursor_navigatePage_waypoint) {
      uint8_t selectionBoxHeight = 18;
      display_selectionBox(varioBarWidth + 1, waypointNameY - 14, 96 - varioBarWidth - 1,
                           selectionBoxHeight, 5);
    }

    // Progress Bar
    float percent_progress;
    if (navigator.navigating) {
      percent_progress = 1 - navigator.pointDistanceRemaining / navigator.segmentDistance;
      if (percent_progress < 0) percent_progress = 0;
    } else {
      percent_progress = 0;
    }
    u8g2.drawFrame(varioBarWidth - 1, topOfFrame + varioBarHeight - 4, 96 - varioBarWidth + 1, 4);
    u8g2.drawBox(varioBarWidth, topOfFrame + varioBarHeight - 3,
                 percent_progress * (96 - varioBarWidth), 2);

    // User Fields ****************************************************

    // Layout
    uint8_t userFieldsRow1Y = topOfFrame + varioBarHeight;
    uint8_t userFieldsRow2Y = 20 + userFieldsRow1Y;
    uint8_t userFieldsCol1X = 3;
    uint8_t userFieldsCol2X = 52;

    //  1 | 2
    // ---|---
    //  3 | 4

    // User Field 1 -- Time to waypoint
    display_waypointTimeRemaining(userFieldsCol1X + 5, userFieldsRow1Y + 20, leaf_6x12);
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(userFieldsCol1X, userFieldsRow1Y + 7);
    u8g2.print("TIME>&");
    u8g2.setFont(leaf_6x12);

    // User Field 2  -- Dist to waypoint
    double displayDistance = 0;
    if (navigator.navigating) displayDistance = navigator.pointDistanceRemaining;
    display_distance(userFieldsCol2X + 5, userFieldsRow1Y + 20, displayDistance);
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(userFieldsCol2X, userFieldsRow1Y + 7);
    u8g2.print("DIST>&");
    u8g2.setFont(leaf_6x12);

    // User Field 3 -- Glide Ratio
    display_glide(userFieldsCol1X + 5, userFieldsRow2Y + 20, gps.getGlideRatio());
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(userFieldsCol1X, userFieldsRow2Y + 7);
    u8g2.print("`GLIDE");
    u8g2.setFont(leaf_6x12);

    // User Field 4	-- Glide Ratio to Waypoint
    display_glide(userFieldsCol2X + 5, userFieldsRow2Y + 20, navigator.glideToActive);
    u8g2.setFont(leaf_5h);
    u8g2.setCursor(userFieldsCol2X, userFieldsRow2Y + 7);
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
    if (navigator.navigating) {
      if (navigator.activeRouteIndex) {
        destination_selection_index = navigator.activeRouteIndex;
        destination_selection_routes_vs_waypoints = true;
      } else if (navigator.activeWaypointIndex) {
        destination_selection_index = navigator.activeWaypointIndex;
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
          if (state == RELEASED)
            settings.adjustDisplayField_navPage_alt(Button::CENTER);
          else if (state == HELD && count == 1 && settings.disp_navPageAltType == altType_MSL) {
            if (baro.syncToGPSAlt()) {  // successful adjustment of altimeter setting to match
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
            navigator.cancelNav();
            navigatePage_cursorPosition = cursor_navigatePage_none;
            buttons_lockAfterHold();  // lock buttons so we don't turn off if user keeps holding
                                      // button
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
          if (state == RELEASED && !flightTimer_isRunning()) {
            flightTimer_start();
            navigatePage_cursorPosition = cursor_navigatePage_none;
          } else if (state == HELD && flightTimer_isRunning()) {
            flightTimer_stop();
            navigatePage_cursorPosition = cursor_navigatePage_none;
            buttons_lockAfterHold();  // lock buttons so we don't turn off if user keeps holding
                                      // button
          }

          break;
      }
      break;
  }
}