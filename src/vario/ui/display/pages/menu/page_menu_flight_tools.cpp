#include "ui/display/pages/menu/page_menu_flight_tools.h"

#include <Arduino.h>

#include "instruments/baro.h"
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
};

void FlightToolsMenuPage::draw() {
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Flight", menu_ui::GLYPH_FLIGHT);

    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 76;
    uint8_t menu_items_y[] = {190, 45, 60, 75};

    for (int i = 0; i <= cursor_max; i++) {
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
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void FlightToolsMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
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
