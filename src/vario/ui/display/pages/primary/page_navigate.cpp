#include <Arduino.h>
#include <U8g2lib.h>
#ifdef LEAF_SIM
#include "route_guidance.h"
#endif

#include "hardware/buttons.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
#include "navigation/gpx.h"
#include "navigation/nav_ids.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

enum navigate_page_items {
  cursor_navigatePage_none,
  cursor_navigatePage_alt1,
  cursor_navigatePage_waypoint,
  cursor_navigatePage_field1,
  cursor_navigatePage_field2,
  cursor_navigatePage_field3,
  cursor_navigatePage_field4,
  cursor_navigatePage_timer
};

int8_t navigatePage_cursorPosition = cursor_navigatePage_none;
uint8_t navigatePage_cursorTimeCount =
    0;  // count up for every page_draw, and if no button is pressed, then reset cursor to "none"
        // after the timeOut value is reached.
uint8_t navigatePage_cursorTimeOut =
    8;  // after 8 page draws (4 seconds) reset the cursor if a button hasn't been pushed.

enum class DestinationSelectionMode : uint8_t {
  Routes,
  Waypoints,
  ActiveRoutePoints,
};

int16_t destination_selection_index = 0;
DestinationSelectionMode destination_selection_mode = DestinationSelectionMode::Routes;

bool selectingDestinationRoutes() {
  return destination_selection_mode == DestinationSelectionMode::Routes;
}

RouteID routePointSelectionRoute() { return navigator.routeContextIndex(); }

bool selectingActiveRoutePoints() {
  return destination_selection_mode == DestinationSelectionMode::ActiveRoutePoints &&
         routePointSelectionRoute();
}

void navigatePage_destinationSelect(Button dir) {
  switch (dir) {
    case Button::LEFT:
      if (selectingActiveRoutePoints()) {
        const Route& route = navigator.routes[routePointSelectionRoute()];
        destination_selection_index =
            destination_selection_index <= 0 ? route.totalPoints : destination_selection_index - 1;
      } else if (destination_selection_index == 0) {
        if (navigator.totalWaypoints >= 1) {
          destination_selection_mode = DestinationSelectionMode::Waypoints;
          destination_selection_index = navigator.totalWaypoints;
        } else if (navigator.totalRoutes >= 1) {
          destination_selection_mode = DestinationSelectionMode::Routes;
          destination_selection_index = navigator.totalRoutes;
        }
      } else if (!selectingDestinationRoutes() && destination_selection_index == 1) {
        if (navigator.totalRoutes >= 1) {
          destination_selection_mode = DestinationSelectionMode::Routes;
          destination_selection_index = navigator.totalRoutes;
        } else {
          destination_selection_index = 0;
        }
      } else {
        destination_selection_index--;
      }
      break;

    case Button::RIGHT:
      if (selectingActiveRoutePoints()) {
        const Route& route = navigator.routes[routePointSelectionRoute()];
        destination_selection_index++;
        if (destination_selection_index > route.totalPoints) destination_selection_index = 0;
      } else {
        destination_selection_index++;
      }
      if (selectingDestinationRoutes()) {  // if we're currently showing Routes...
        if (destination_selection_index >
            navigator.totalRoutes) {            // 		if we're passed the number of Routes we have...
          if (navigator.totalWaypoints >= 1) {  // 		...and we have waypoints to switch
                                                // to...
            destination_selection_mode = DestinationSelectionMode::Waypoints;  // switch to wpts
            destination_selection_index = 1;  // and show the first one
          } else {                            // otherwise loop back to index 0
            destination_selection_index = 0;
          }
        }
      } else if (!selectingActiveRoutePoints()) {  // otherise if we're showing waypoints...
        if (destination_selection_index >
            navigator.totalWaypoints) {  //...and we're passed the number of waypoints we have
          destination_selection_mode = navigator.totalRoutes >= 1
                                           ? DestinationSelectionMode::Routes
                                           : DestinationSelectionMode::Waypoints;
          destination_selection_index = 0;
        }
      }
      break;
    case Button::CENTER:
      if (selectingActiveRoutePoints()) {
        navigator.activateRoute(routePointSelectionRoute(),
                                destination_selection_index <= 0
                                    ? RouteIndex(1)
                                    : RouteIndex(destination_selection_index));
      } else if (destination_selection_index == 0) {
        break;
      } else if (selectingDestinationRoutes()) {
        navigator.activateRoute(RouteID(destination_selection_index));
      } else {
        navigator.activatePoint(WaypointID(destination_selection_index));
      }
      navigatePage_cursorPosition = cursor_navigatePage_none;
      break;
  }
}

void navigatePage_reset() {
  navigatePage_cursorPosition = cursor_navigatePage_none;
  navigatePage_cursorTimeCount = 0;
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
    bool showHeadingTurnArrows = navigator.hasNavSolution();
    display_headerAndFooter(navigatePage_cursorPosition == cursor_navigatePage_timer,
                            showHeadingTurnArrows);

    // Fixed heading marker: one-pixel open outline, with a clear channel
    // for the active direction arrow when it points straight ahead.
    const auto drawHeadingMarker = [&] {
      const int pointer_x = nav_x, pointer_y = nav_y - nav_r - 3;
      for (int row = 0; row <= 10; ++row) {
        const int halfWidth = 1 + (row + 1) / 2;
        u8g2.setDrawColor(0);
        u8g2.drawHLine(pointer_x - halfWidth + 1, pointer_y + row, 2 * halfWidth - 1);
        u8g2.setDrawColor(1);
        if (row == 0)
          u8g2.drawHLine(pointer_x - 1, pointer_y, 3);
        else {
          u8g2.drawPixel(pointer_x - halfWidth, pointer_y + row);
          u8g2.drawPixel(pointer_x + halfWidth, pointer_y + row);
        }
      }
      u8g2.setDrawColor(0);
      u8g2.drawBox(pointer_x - 3, pointer_y + 11, 7, 3);
      u8g2.setDrawColor(1);
      u8g2.drawVLine(pointer_x - 3, pointer_y + 11, 3);
      u8g2.drawVLine(pointer_x + 3, pointer_y + 11, 3);
    };

    drawHeadingMarker();

    // Active direction: trim the outer two tip pixels into a three-pixel flat cap.
    // The remaining head and stem retain the reference silhouette.
    if (navigator.hasNavSolution()) {
      const double angle = navigator.turnToActive * DEG_TO_RAD;
      const double sine = sin(angle), cosine = cos(angle);
      struct Point {
        int x, y;
      };
      const auto rotated = [&](int across, int forward) {
        return Point{int(lround(nav_x + across * cosine + forward * sine)),
                     int(lround(nav_y + across * sine - forward * cosine))};
      };
      const auto tipLeft = rotated(-1, nav_r);
      const auto tipRight = rotated(1, nav_r);
      const auto left = rotated(-4, nav_r - 7);
      const auto right = rotated(4, nav_r - 7);
      const auto stemTopLeft = rotated(-1, nav_r - 7);
      const auto stemTopRight = rotated(1, nav_r - 7);
      const auto stemBottomLeft = rotated(-1, nav_r - 10);
      const auto stemBottomRight = rotated(1, nav_r - 10);
      const auto line = [&](Point a, Point b) { u8g2.drawLine(a.x, a.y, b.x, b.y); };
      u8g2.setDrawColor(1);
      u8g2.drawTriangle(stemTopLeft.x, stemTopLeft.y, stemTopRight.x, stemTopRight.y,
                        stemBottomLeft.x, stemBottomLeft.y);
      u8g2.drawTriangle(stemTopRight.x, stemTopRight.y, stemBottomRight.x, stemBottomRight.y,
                        stemBottomLeft.x, stemBottomLeft.y);
      line(stemTopLeft, stemTopRight);
      line(stemTopRight, stemBottomRight);
      line(stemBottomRight, stemBottomLeft);
      line(stemBottomLeft, stemTopLeft);
      line(stemTopRight, stemBottomLeft);
      u8g2.drawTriangle(left.x, left.y, tipLeft.x, tipLeft.y, tipRight.x, tipRight.y);
      u8g2.drawTriangle(left.x, left.y, tipRight.x, tipRight.y, right.x, right.y);
      // u8g2 deliberately omits a triangle edge: stroke after filling.
      line(left, tipLeft);
      line(tipLeft, tipRight);
      line(tipRight, right);
      line(right, left);
      line(left, tipRight);
    }
    // Upcoming optimized intercept, drawn inside the active pointer's radius.
    double followingTurn = 0;
    if (navigator.followingTurn(followingTurn)) {
      const double angle = followingTurn * DEG_TO_RAD;
      const double tipRadius = nav_r - 10, baseRadius = tipRadius - 5;
      const int tipX = lround(nav_x + sin(angle) * tipRadius);
      const int tipY = lround(nav_y - cos(angle) * tipRadius);
      const int baseX = lround(nav_x + sin(angle) * baseRadius);
      const int baseY = lround(nav_y - cos(angle) * baseRadius);
      const int leftX = lround(baseX - cos(angle) * 5);
      const int leftY = lround(baseY - sin(angle) * 5);
      const int rightX = lround(baseX + cos(angle) * 5);
      const int rightY = lround(baseY + sin(angle) * 5);
      u8g2.setDrawColor(1);
      u8g2.drawLine(tipX, tipY, leftX, leftY);
      u8g2.drawLine(tipX, tipY, rightX, rightY);
      u8g2.drawLine(leftX, leftY, rightX, rightY);
    }
    // wind & compass
    uint8_t wind_radius = 12;
    uint8_t pointer_size = 7;
    bool showPointer = false;
    display_windSockRing(nav_x, nav_y, wind_radius, pointer_size, showPointer);

    ///////////////////////////////////////////////////
    // Vario Info *************************************

    // Vario Bar
    // TODO: display lack of climb rate differently than 0
    int32_t climbRate = baro.climbRateFilteredValid() ? baro.climbRateFiltered() : 0;
    const int32_t displayClimbRate = baro.climbRateFilteredValid() ? baro.climbRateForDisplay() : 0;
    display_varioBar(topOfFrame, varioBarTopHeight, varioBarBottomHeight, varioBarWidth, climbRate);

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
    if (climbRate >= 0)
      u8g2.print("+");
    else
      u8g2.print("-");

    // climb value
    display_unsignedClimbRate_short(varioBarWidth + 1, varioBarMidpoint + 20, displayClimbRate);

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
    // display_altAboveLaunch(24, 129);

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
      if (selectingActiveRoutePoints()) {
        const RouteID routeIndex = routePointSelectionRoute();
        const Route& route = navigator.routes[routeIndex];
        if (destination_selection_index <= 0) {
          u8g2.setFont(leaf_icons);
          u8g2.print('R');
          u8g2.setFont(u8g2_font_12x6LED_tf);
          u8g2.print(route.name);
        } else {
          u8g2.setFont(leaf_5x8);
          u8g2.print(destination_selection_index);
          u8g2.print('/');
          u8g2.print(route.totalPoints);
          u8g2.print(' ');
          u8g2.setFont(u8g2_font_12x6LED_tf);
          u8g2.print(
              navigator.routePoint(routeIndex, RouteIndex(destination_selection_index)).name);
        }
      } else if (destination_selection_index == 0) {
        u8g2.print(defaultWaypointString);
      } else {
        if (selectingDestinationRoutes()) {
          u8g2.setFont(leaf_icons);
          u8g2.print('R');
          u8g2.setFont(u8g2_font_12x6LED_tf);
          u8g2.print(navigator.routes[destination_selection_index].name);
        } else {
          u8g2.print(navigator.waypoint(WaypointID(destination_selection_index)).name);
        }
      }
    } else {
      if (navigator.navigating)
        u8g2.print(navigator.activePoint.name);
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
    if (navigator.hasNavSolution() && navigator.segmentDistance > 0) {
      const double distanceToSequence = navigator.cylinderDistanceRemaining();
      const double segmentToSequence = navigator.cylinderInitialLegDistance();
      percent_progress = segmentToSequence > 0 ? 1 - distanceToSequence / segmentToSequence : 1;
      if (percent_progress < 0) percent_progress = 0;
      if (percent_progress > 1) percent_progress = 1;
    } else {
      percent_progress = 0;
    }
    u8g2.drawFrame(varioBarWidth - 1, topOfFrame + varioBarHeight - 4, 96 - varioBarWidth + 1, 4);
    u8g2.drawBox(varioBarWidth, topOfFrame + varioBarHeight - 3,
                 percent_progress * (96 - varioBarWidth), 2);

    // Four independently configurable fields, in two columns and two rows.
    // In leaf_5h, '*' (0x2A) is the waypoint diamond and '%' (0x25) is the cylinder ring.
    const char* labels[] = {"TIME>*", "TIME>GOAL", "DIST>*", "DIST>GOAL", "`GLIDE",
                            "`>*",    "`>GOAL",    "ALT^*",  "ALT^GOAL",  "TIME>%",
                            "DIST>%", "`>%",       "ALT^%"};
    static_assert(sizeof(labels) / sizeof(labels[0]) == static_cast<uint8_t>(NavField::Count));
    for (uint8_t i = 0; i < 4; ++i) {
      const uint8_t x = i % 2 == 0 ? 3 : 52;
      const uint8_t y = topOfFrame + varioBarHeight + (i / 2) * 21;
      const bool selected = navigatePage_cursorPosition == cursor_navigatePage_field1 + i;
      const NavField field = settings.disp_navFields[i];
      u8g2.setFont(leaf_6x12);
      switch (field) {
        case NavField::TimeToWaypoint:
        case NavField::TimeToCylinder:
        case NavField::TimeToGoal: {
          const uint32_t seconds = !navigator.hasNavSolution()     ? 0
                                   : field == NavField::TimeToGoal ? navigator.goalTimeRemaining()
                                   : field == NavField::TimeToCylinder
                                       ? navigator.cylinderTimeRemaining()
                                       : navigator.pointTimeRemaining;
          // Seven-character hour strings still fit inside the narrow selection box.
          display_navTimeRemaining(x + 5, y + 20, seconds >= 3600 ? leaf_5x8 : leaf_6x12, seconds);
          break;
        }
        case NavField::DistanceToWaypoint:
        case NavField::DistanceToCylinder:
        case NavField::DistanceToGoal:
          display_distance(x + 5, y + 20,
                           !navigator.hasNavSolution()         ? 0
                           : field == NavField::DistanceToGoal ? navigator.goalDistanceRemaining()
                           : field == NavField::DistanceToCylinder
                               ? navigator.cylinderDistanceRemaining()
                               : navigator.pointDistanceRemaining,
                           38, navigator.hasNavSolution());
          break;
        case NavField::CurrentGlide:
          display_glide(x + 5, y + 20, gps.getGlideRatio());
          break;
        case NavField::GlideToWaypoint:
          display_glide(x + 5, y + 20, navigator.hasNavSolution() ? navigator.glideToActive : 0);
          break;
        case NavField::GlideToCylinder:
          display_glide(x + 5, y + 20, navigator.glideToCylinder());
          break;
        case NavField::GlideToGoal:
          display_glide(x + 5, y + 20, navigator.glideToGoal());
          break;
        case NavField::AltitudeAboveWaypoint:
          display_navAltitudeSurplus(x, y + 20, navigator.arrivalAltitudeAboveWaypoint());
          break;
        case NavField::AltitudeAboveCylinder:
          display_navAltitudeSurplus(x, y + 20, navigator.arrivalAltitudeAboveCylinder());
          break;
        case NavField::AltitudeAboveGoal:
          display_navAltitudeSurplus(x, y + 20, navigator.arrivalAltitudeAboveGoal());
          break;
        case NavField::Count:
          break;
      }
      u8g2.setDrawColor(1);
      u8g2.setFont(leaf_5h);
      u8g2.setCursor(x, y + 7);
      u8g2.print(labels[static_cast<uint8_t>(field)]);
      if (selected) display_selectionBox(x - 3, y, i % 2 == 0 ? 48 : 47, 21, 3);
    }

    // Footer Info ****************************************************

  } while (u8g2.nextPage());
}

void nav_cursor_move(Button button) {
  // Vertical movement stays in the selected column. The timer enters the right column.
  if (button == Button::UP) {
    switch (navigatePage_cursorPosition) {
      case cursor_navigatePage_none:
        navigatePage_cursorPosition = cursor_navigatePage_timer;
        break;
      case cursor_navigatePage_timer:
        navigatePage_cursorPosition = cursor_navigatePage_field4;
        break;
      case cursor_navigatePage_field3:
        navigatePage_cursorPosition = cursor_navigatePage_field1;
        break;
      case cursor_navigatePage_field4:
        navigatePage_cursorPosition = cursor_navigatePage_field2;
        break;
      case cursor_navigatePage_field1:
      case cursor_navigatePage_field2:
        navigatePage_cursorPosition = cursor_navigatePage_waypoint;
        break;
      default:
        --navigatePage_cursorPosition;
        break;
    }
  } else if (button == Button::DOWN) {
    switch (navigatePage_cursorPosition) {
      case cursor_navigatePage_waypoint:
        navigatePage_cursorPosition = cursor_navigatePage_field2;
        break;
      case cursor_navigatePage_field1:
        navigatePage_cursorPosition = cursor_navigatePage_field3;
        break;
      case cursor_navigatePage_field2:
        navigatePage_cursorPosition = cursor_navigatePage_field4;
        break;
      case cursor_navigatePage_field3:
      case cursor_navigatePage_field4:
        navigatePage_cursorPosition = cursor_navigatePage_timer;
        break;
      case cursor_navigatePage_timer:
        navigatePage_cursorPosition = cursor_navigatePage_none;
        break;
      default:
        ++navigatePage_cursorPosition;
        break;
    }
  } else if (button == Button::LEFT || button == Button::RIGHT) {
    const uint8_t field = navigatePage_cursorPosition - cursor_navigatePage_field1;
    navigatePage_cursorPosition = cursor_navigatePage_field1 + (field ^ 1);
  }
  speaker.playSound(navigatePage_cursorPosition == cursor_navigatePage_none ? fx::doubleClick
                                                                            : fx::click);

  if (navigatePage_cursorPosition == cursor_navigatePage_waypoint) {
    if (navigator.navigating) {
      if (navigator.activeRouteIndex) {
        destination_selection_mode = DestinationSelectionMode::ActiveRoutePoints;
        destination_selection_index =
            navigator.activeRoutePointIndex ? navigator.activeRoutePointIndex : RouteIndex(1);
      } else if (navigator.activeWaypointIndex) {
        destination_selection_mode = DestinationSelectionMode::Waypoints;
        destination_selection_index = navigator.activeWaypointIndex;
      }
    } else if (navigator.routeContextIndex()) {
      destination_selection_mode = DestinationSelectionMode::ActiveRoutePoints;
      destination_selection_index = 0;
    }
  }
}

bool navigatePage_volumeShortcut(Button button, ButtonEvent state) {
  if (!settings.volumeShortcut || (button != Button::UP && button != Button::DOWN) ||
      state != ButtonEvent::INCREMENTED) {
    return false;
  }

  if (!settings.adjustShortcutVolume(button)) buttons.consumeButton();
  return true;
}

void navigatePage_button(Button button, ButtonEvent state, uint8_t count) {
  // reset cursor time out count if a button is pushed
  navigatePage_cursorTimeCount = 0;

  switch (navigatePage_cursorPosition) {
    case cursor_navigatePage_none:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (navigatePage_volumeShortcut(button, state)) {
            break;
          } else if (state == ButtonEvent::CLICKED) {
            nav_cursor_move(button);
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
    case cursor_navigatePage_alt1:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) nav_cursor_move(button);
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
          if (state == ButtonEvent::CLICKED)
            settings.adjustDisplayField_navPage_alt(Button::CENTER);
          else if (state == ButtonEvent::INCREMENTED && count == 1 &&
                   settings.disp_navPageAltType == altType_MSL) {
            if (baro.syncToGPSAlt()) {  // successful adjustment of altimeter setting to match
                                        // GPS altitude
              speaker.playSound(fx::enter);
              navigatePage_cursorPosition = cursor_navigatePage_none;
            } else {  // unsuccessful
              speaker.playSound(fx::cancel);
            }
          }
          break;
      }
      break;
    case cursor_navigatePage_waypoint:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) nav_cursor_move(button);
          break;
        case Button::LEFT:
          if (state == ButtonEvent::CLICKED) navigatePage_destinationSelect(Button::LEFT);
          break;
        case Button::RIGHT:
          if (state == ButtonEvent::CLICKED) navigatePage_destinationSelect(Button::RIGHT);
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED) navigatePage_destinationSelect(Button::CENTER);
          if (state == ButtonEvent::HELD) {
            buttons.consumeButton();
            navigator.cancelNav();
            navigatePage_cursorPosition = cursor_navigatePage_none;
          }
          break;
      }
      break;
    case cursor_navigatePage_field1:
    case cursor_navigatePage_field2:
    case cursor_navigatePage_field3:
    case cursor_navigatePage_field4:
      if (state == ButtonEvent::CLICKED) {
        if (button == Button::CENTER) {
          NavField& field =
              settings.disp_navFields[navigatePage_cursorPosition - cursor_navigatePage_field1];
          field = static_cast<NavField>((static_cast<uint8_t>(field) + 1) %
                                        static_cast<uint8_t>(NavField::Count));
          speaker.playSound(fx::neutral);
        } else {
          nav_cursor_move(button);
        }
      }
      break;
    case cursor_navigatePage_timer:
      switch (button) {
        case Button::UP:
        case Button::DOWN:
          if (state == ButtonEvent::CLICKED) nav_cursor_move(button);
          break;
        case Button::LEFT:
          break;
        case Button::RIGHT:
          break;
        case Button::CENTER:
          if (state == ButtonEvent::CLICKED && !flightTimer_isRunning()) {
            flightTimer_start();
            navigatePage_cursorPosition = cursor_navigatePage_none;
          } else if (state == ButtonEvent::HELD && flightTimer_isRunning()) {
            buttons.consumeButton();
            flightTimer_stop();
            navigatePage_cursorPosition = cursor_navigatePage_none;
          }

          break;
      }
      break;
  }
  display.update();
}
