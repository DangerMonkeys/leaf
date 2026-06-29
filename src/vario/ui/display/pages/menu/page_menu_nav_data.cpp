#include "ui/display/pages/menu/page_menu_nav_data.h"

#include <Arduino.h>

#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/settings/settings.h"

enum nav_data_menu_items {
  cursor_nav_data_back,
  cursor_nav_data_loadGpx,
  cursor_nav_data_selectPoint,
  cursor_nav_data_selectRoute,
  cursor_nav_data_buildRoute,
  cursor_nav_data_savePoint,
};

namespace {
  constexpr char* labels[6] = {"Back",       "Load GPX",  "Select Pt.",
                               "Select Rte", "Build Rte", "Save Point"};
  constexpr uint8_t glyphs[6] = {0,
                                 menu_ui::GLYPH_NAV_DATA,
                                 menu_ui::GLYPH_NAV_POINT_SELECT,
                                 menu_ui::GLYPH_NAV_ROUTE_SELECT,
                                 menu_ui::GLYPH_NAV_ROUTE_BUILD,
                                 menu_ui::GLYPH_NAV_POINT_SAVE};
}  // namespace

void NavDataMenuPage::draw() {
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Nav Data", menu_ui::GLYPH_NAV_DATA);

    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 76;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 105};

    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      if (i == cursor_nav_data_back) {
        menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
        menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
      } else {
        menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i], glyphs[i]);
        if (selected) {
          u8g2.setCursor(menu_ui::ICON_X, menu_items_y[i]);
          u8g2.print('x');
        }
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void NavDataMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
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
    default:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
      }
      break;
  }
}
