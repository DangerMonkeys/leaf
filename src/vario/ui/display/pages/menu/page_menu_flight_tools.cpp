#include "ui/display/pages/menu/page_menu_flight_tools.h"

#include <Arduino.h>

#include "logging/log.h"
#include "navigation/gpx.h"
#include "navigation/user_waypoints.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/settings/settings.h"

enum flight_tools_menu_items {
  cursor_flight_tools_back,
  cursor_flight_tools_profiles,
  cursor_flight_tools_varioVolume,
  cursor_flight_tools_savePoint,
  cursor_flight_tools_navHeader,
  cursor_flight_tools_navStatus,
  cursor_flight_tools_resumeRoute,
  cursor_flight_tools_resumePoint,
  cursor_flight_tools_resetRoute,
  cursor_flight_tools_restartRoute,
  cursor_flight_tools_cancelNav,
};

bool FlightToolsMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
  switch (button) {
    case Button::UP:
      if (state == ButtonEvent::CLICKED) {
        cursor_prev();
        skip_hidden_backward();
        playCursorMoveSound();
      }
      break;
    case Button::DOWN:
      if (state == ButtonEvent::CLICKED) {
        cursor_next();
        skip_hidden_forward();
        playCursorMoveSound();
      }
      break;
    case Button::LEFT:
      if (state == ButtonEvent::CLICKED && cursor_position != cursor_flight_tools_back &&
          cursor_position != cursor_flight_tools_varioVolume) {
        speaker.playSound(fx::cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
        break;
      }
      [[fallthrough]];
    case Button::RIGHT:
    case Button::CENTER:
      setting_change(button, state, count);
      break;
    default:
      break;
  }

  return button != Button::NONE;
}

void FlightToolsMenuPage::draw() {
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Flight", menu_ui::GLYPH_FLIGHT);

    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 76;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 112, 126, 141, 141, 156, 156, 171};

    for (int i = 0; i <= cursor_max; i++) {
      if (row_hidden(i)) continue;

      const bool selected = i == cursor_position;
      const bool selectable = row_selectable(i);
      u8g2.setFont(leaf_6x12);
      if (selectable) menu_ui::beginRow(menu_items_y[i], selected);
      switch (i) {
        case cursor_flight_tools_back:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Back");
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
        case cursor_flight_tools_profiles:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Profiles", menu_ui::GLYPH_PROFILE);
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        case cursor_flight_tools_varioVolume:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "VarioVol", menu_ui::GLYPH_VARIO);
          u8g2.setCursor(setting_choice_x, menu_items_y[i]);
          u8g2.print(' ');
          u8g2.setFont(leaf_icons);
          u8g2.print(char('I' + settings.vario_volume));
          u8g2.setFont(leaf_6x12);
          break;
        case cursor_flight_tools_savePoint:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Save Point",
                             menu_ui::GLYPH_NAV_POINT_SAVE);
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        case cursor_flight_tools_navHeader:
          u8g2.setFont(leaf_5x8);
          u8g2.setCursor(setting_name_x, menu_items_y[i] - 1);
          u8g2.print("Navigation");
          u8g2.drawHLine(0, menu_items_y[i], 96);
          break;
        case cursor_flight_tools_navStatus:
          drawNavStatusLine(menu_items_y[i]);
          break;
        case cursor_flight_tools_resumeRoute:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Resume Rte",
                             menu_ui::GLYPH_NAV_ROUTE_SELECT);
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        case cursor_flight_tools_resumePoint:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Resume Pt",
                             menu_ui::GLYPH_NAV_POINT_SELECT);
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        case cursor_flight_tools_resetRoute:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Reset Rte",
                             menu_ui::GLYPH_RESET_ROUTE);
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        case cursor_flight_tools_restartRoute:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Restart Rte",
                             menu_ui::GLYPH_RESET_ROUTE);
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        case cursor_flight_tools_cancelNav:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Cancel Nav",
                             menu_ui::GLYPH_CANCEL_NAV);
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
      }
      if (selectable) menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void FlightToolsMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (!row_selectable(cursor_position)) return;

  switch (cursor_position) {
    case cursor_flight_tools_profiles:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        speaker.playSound(fx::increase);
        profileSelectPage.show();
      }
      break;
    case cursor_flight_tools_varioVolume:
      if (state == ButtonEvent::CLICKED && (dir == Button::LEFT || dir == Button::RIGHT)) {
        settings.adjustVolumeVario(dir);
      }
      break;
    case cursor_flight_tools_savePoint:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        Waypoint savedWaypoint;
        String error;
        if (user_waypoints::appendCurrentPosition(savedWaypoint, error)) {
          flightTimer_markSavedPoint(savedWaypoint);
          speaker.playSound(fx::confirm);
        } else {
          Serial.print("Save Point failed: ");
          Serial.println(error);
          speaker.playSound(fx::bad);
        }
        cursor_position = cursor_flight_tools_back;
      }
      break;
    case cursor_flight_tools_resumeRoute:
    case cursor_flight_tools_resumePoint:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        if (navigator.resumeLastNav()) {
          speaker.playSound(fx::confirm);
        } else {
          speaker.playSound(fx::bad);
        }
        cursor_position = cursor_flight_tools_back;
      }
      break;
    case cursor_flight_tools_resetRoute:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        if (navigator.activeRouteIndex && navigator.activateRoute(navigator.activeRouteIndex)) {
          speaker.playSound(fx::confirm);
        } else {
          speaker.playSound(fx::bad);
        }
        cursor_position = cursor_flight_tools_back;
      }
      break;
    case cursor_flight_tools_restartRoute:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        if (navigator.restartLastRoute()) {
          speaker.playSound(fx::confirm);
        } else {
          speaker.playSound(fx::bad);
        }
        cursor_position = cursor_flight_tools_back;
      }
      break;
    case cursor_flight_tools_cancelNav:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        navigator.cancelNav();
        cursor_position = cursor_flight_tools_back;
      }
      break;
    case cursor_flight_tools_back:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        mainMenuPage.quitMenu();
      }
      break;
  }
}

bool FlightToolsMenuPage::row_selectable(int8_t row) const {
  return !row_hidden(row) && row != cursor_flight_tools_navHeader &&
         row != cursor_flight_tools_navStatus;
}

bool FlightToolsMenuPage::row_hidden(int8_t row) const {
  if (row == cursor_flight_tools_navHeader || row == cursor_flight_tools_navStatus)
    return !nav_section_visible();
  if (row == cursor_flight_tools_resumeRoute)
    return navigator.hasActivePoint() || !navigator.hasLastNav() || !navigator.lastNavIsRoute();
  if (row == cursor_flight_tools_resumePoint)
    return navigator.hasActivePoint() || !navigator.hasLastNav() || !navigator.lastNavIsPoint();
  if (row == cursor_flight_tools_resetRoute) return !navigator.activeRouteIndex;
  if (row == cursor_flight_tools_restartRoute)
    return navigator.hasActivePoint() || !navigator.hasLastNav() || !navigator.lastNavIsRoute();
  if (row == cursor_flight_tools_cancelNav) return !navigator.hasActivePoint();
  return false;
}

bool FlightToolsMenuPage::nav_section_visible() const {
  return navigator.hasActivePoint() || navigator.hasLastNav();
}

void FlightToolsMenuPage::drawNavStatusLine(uint8_t y) const {
  const bool active = navigator.hasActivePoint();
  const bool route = active ? navigator.activeRouteIndex : navigator.lastNavIsRoute();
  const char* name = "";
  if (active) {
    name = route ? navigator.routes[navigator.activeRouteIndex].name : navigator.activePoint.name;
  } else {
    name = navigator.lastNavDestinationName();
  }

  u8g2.setFont(leaf_5x8);
  u8g2.setCursor(2, y);
  u8g2.print(active ? "Active:" : "Last:");

  u8g2.setFont(leaf_6x12);
  u8g2.print((char)(route ? menu_ui::GLYPH_ROUTE : menu_ui::GLYPH_WAYPOINT));

  u8g2.setFont(u8g2_font_12x6LED_tf);
  u8g2.print(name);
}

void FlightToolsMenuPage::skip_hidden_forward() {
  while (!row_selectable(cursor_position)) cursor_next();
}

void FlightToolsMenuPage::skip_hidden_backward() {
  while (!row_selectable(cursor_position)) cursor_prev();
}
