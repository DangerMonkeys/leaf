#include "ui/display/pages/menu/page_menu_flight_tools.h"

#include <Arduino.h>

#include "instruments/baro.h"
#include "navigation/gpx.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/settings/settings.h"

enum flight_tools_menu_items {
  cursor_flight_tools_back,
  cursor_flight_tools_syncAlt,
  cursor_flight_tools_varioVolume,
  cursor_flight_tools_savePoint,
  cursor_flight_tools_resetRoute,
  cursor_flight_tools_cancelNav,
};

bool FlightToolsMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
  switch (button) {
    case Button::UP:
      if (state == ButtonEvent::CLICKED) {
        cursor_prev();
        skip_hidden_backward();
      }
      break;
    case Button::DOWN:
      if (state == ButtonEvent::CLICKED) {
        cursor_next();
        skip_hidden_forward();
      }
      break;
    case Button::LEFT:
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
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 105};

    for (int i = 0; i <= cursor_max; i++) {
      if (row_hidden(i)) continue;

      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      switch (i) {
        case cursor_flight_tools_back:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Back");
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
        case cursor_flight_tools_syncAlt:
          u8g2.setCursor(setting_name_x, menu_items_y[i]);
          menu_ui::printGlyph(menu_ui::GLYPH_GPS);
          menu_ui::printGlyph(menu_ui::GLYPH_ALTIMETER);
          u8g2.print("SyncAlt");
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
        case cursor_flight_tools_resetRoute:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Reset Rte",
                             menu_ui::GLYPH_RESET_ROUTE);
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
        case cursor_flight_tools_cancelNav:
          menu_ui::drawLabel(setting_name_x, menu_items_y[i], "Cancel Nav",
                             menu_ui::GLYPH_CANCEL_NAV);
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void FlightToolsMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (row_hidden(cursor_position)) return;

  switch (cursor_position) {
    case cursor_flight_tools_syncAlt:
      if (state == ButtonEvent::CLICKED) {
        if (baro.syncToGPSAlt()) {
          speaker.playSound(fx::enter);
        } else {
          speaker.playSound(fx::cancel);
        }
      }
      break;
    case cursor_flight_tools_varioVolume:
      if (state == ButtonEvent::CLICKED && (dir == Button::LEFT || dir == Button::RIGHT)) {
        settings.adjustVolumeVario(dir);
      }
      break;
    case cursor_flight_tools_savePoint:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
      }
      break;
    case cursor_flight_tools_resetRoute:
      if (state == ButtonEvent::CLICKED) {
        if (navigator.activeRouteIndex && navigator.activateRoute(navigator.activeRouteIndex)) {
          speaker.playSound(fx::confirm);
        } else {
          speaker.playSound(fx::bad);
        }
      }
      break;
    case cursor_flight_tools_cancelNav:
      if (state == ButtonEvent::CLICKED) {
        navigator.cancelNav();
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

bool FlightToolsMenuPage::row_hidden(int8_t row) const {
  if (row == cursor_flight_tools_resetRoute) return !navigator.activeRouteIndex;
  if (row == cursor_flight_tools_cancelNav) return !navigator.hasActivePoint();
  return false;
}

void FlightToolsMenuPage::skip_hidden_forward() {
  while (row_hidden(cursor_position)) cursor_next();
}

void FlightToolsMenuPage::skip_hidden_backward() {
  while (row_hidden(cursor_position)) cursor_prev();
}
