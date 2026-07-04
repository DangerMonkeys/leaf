#include "ui/display/pages/menu/page_menu_nav_data.h"

#include <Arduino.h>

#include <string.h>
#include "navigation/gpx.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/settings/settings.h"

enum nav_data_menu_items {
  cursor_nav_data_back,
  cursor_nav_data_selectDest,
  cursor_nav_data_loadFile,
  cursor_nav_data_clearAll,
};

namespace {
  constexpr char* labels[4] = {"Back", "Select Dest", "Load File", "Clear All"};
  constexpr uint8_t glyphs[4] = {0, menu_ui::GLYPH_NAV_POINT_SELECT, menu_ui::GLYPH_NAV_FILE,
                                 menu_ui::GLYPH_CLEAR_ALL};
  constexpr uint8_t MENU_INPUT_X = 76;
  constexpr uint8_t FITTED_TEXT_MAX_WIDTH = 92;
  constexpr uint8_t ACTIVE_SECTION_DIVIDER_Y = 45;
  constexpr uint8_t FILE_SECTION_DIVIDER_Y = 121;
  constexpr uint8_t OPEN_SECTION_DIVIDER_Y = 174;
}  // namespace

void NavDataMenuPage::backToNavDataMenu() { cursor_position = cursor_nav_data_back; }

bool NavDataMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
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

void NavDataMenuPage::draw() {
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Nav Data", menu_ui::GLYPH_NAV_DATA);

    uint8_t setting_name_x = 2;
    uint8_t menu_items_y[] = {190, 94, 149, 164};

    for (int i = 0; i <= cursor_max; i++) {
      if (row_hidden(i)) continue;

      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      if (i == cursor_nav_data_back) {
        menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
        menu_ui::drawBackIcon(MENU_INPUT_X, menu_items_y[i]);
      } else {
        menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i], glyphs[i]);
        menu_ui::drawEnterIcon(MENU_INPUT_X, menu_items_y[i], selected);
      }
      menu_ui::endRow();
    }

    const bool hasActiveRoute = navigator.activeRouteIndex;
    const bool hasActivePoint = !hasActiveRoute && navigator.activeWaypointIndex;
    const char* activeName = hasActiveRoute   ? navigator.routes[navigator.activeRouteIndex].name
                             : hasActivePoint ? navigator.activePoint.name
                                              : "None";
    const char* activeLabel = hasActiveRoute   ? "Active Route:"
                              : hasActivePoint ? "Active Point:"
                                               : "Active Dest:";

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, 28);
    u8g2.print(activeLabel);

    u8g2.setFont(leaf_6x12);
    drawFittedText(2, 41, activeName, FITTED_TEXT_MAX_WIDTH);
    u8g2.drawHLine(0, ACTIVE_SECTION_DIVIDER_Y, 96);

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(2, 56);
    u8g2.print("Active File:");

    if (navigator.hasLoadedNavFile()) {
      u8g2.setFont(leaf_6x12);
      drawFittedText(2, 69, navigator.loadedNavFilename(), FITTED_TEXT_MAX_WIDTH);

      u8g2.setFont(leaf_5x8);
      u8g2.setCursor(2, 79);
      if (navigator.totalRoutes == 0) {
        u8g2.print("Points: ");
        u8g2.print(navigator.totalWaypoints);
      } else {
        u8g2.print("Pts: ");
        u8g2.print(navigator.totalWaypoints);
        u8g2.print("  Rtes: ");
        u8g2.print(navigator.totalRoutes);
      }
    } else {
      u8g2.setFont(leaf_6x12);
      u8g2.setCursor(2, 69);
      u8g2.print("None");
    }

    u8g2.drawHLine(0, FILE_SECTION_DIVIDER_Y, 96);

    u8g2.drawHLine(0, OPEN_SECTION_DIVIDER_Y, 96);
  } while (u8g2.nextPage());
}

void NavDataMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (row_hidden(cursor_position)) return;

  switch (cursor_position) {
    case cursor_nav_data_back:
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
    case cursor_nav_data_selectDest:
      if (state == ButtonEvent::CLICKED && (dir == Button::RIGHT || dir == Button::CENTER)) {
        navDataSelectPage.show();
      } else if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
      }
      break;
    case cursor_nav_data_loadFile:
      if (state == ButtonEvent::CLICKED && (dir == Button::RIGHT || dir == Button::CENTER)) {
        gpxFileSelectPage.show();
      } else if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
      }
      break;
    case cursor_nav_data_clearAll:
      if (state == ButtonEvent::CLICKED && (dir == Button::RIGHT || dir == Button::CENTER)) {
        navigator.clear();
        navigator.clearPersistedState();
        speaker.playSound(fx::confirm);
      } else if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
      }
      break;
  }
}

bool NavDataMenuPage::row_hidden(int8_t row) const {
  if (row == cursor_nav_data_selectDest) {
    return !navigator.hasLoadedNavFile() ||
           (navigator.totalWaypoints == 0 && navigator.totalRoutes == 0);
  }
  return false;
}

void NavDataMenuPage::skip_hidden_forward() {
  while (row_hidden(cursor_position)) cursor_next();
}

void NavDataMenuPage::skip_hidden_backward() {
  while (row_hidden(cursor_position)) cursor_prev();
}

void NavDataMenuPage::drawFittedText(uint8_t x, uint8_t y, const char* text, uint8_t maxWidth) {
  char buffer[maxGpxFileNameLength + 1];
  strncpy(buffer, text, sizeof(buffer));
  buffer[maxGpxFileNameLength] = '\0';

  size_t len = strlen(buffer);
  while (len > 2 && u8g2.getStrWidth(buffer) > maxWidth) {
    buffer[len - 3] = '.';
    buffer[len - 2] = '.';
    buffer[len - 1] = '\0';
    --len;
  }

  u8g2.setCursor(x, y);
  u8g2.print(buffer);
}
