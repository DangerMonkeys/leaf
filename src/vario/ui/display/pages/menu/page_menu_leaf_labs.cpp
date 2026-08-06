#include "ui/display/pages/menu/page_menu_leaf_labs.h"

#include <Arduino.h>

#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum leaf_labs_menu_items {
  cursor_leaf_labs_back,
  cursor_leaf_labs_thermal_track,
};

namespace {
  void setThermalTrackEnabled(bool enabled) {
    if (settings.labs_thermalTrack == enabled) return;

    settings.labs_thermalTrack = enabled;
    settings.disp_showThermalTrackPage = enabled;
    if (!enabled && display.getPage() == MainPage::ThermalTrack) {
      display.setPage(MainPage::User);
    }
    speaker.playSound(enabled ? fx::enter : fx::cancel);
  }
}  // namespace

void LeafLabsMenuPage::draw() {
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Leaf Labs", menu_ui::GLYPH_LEAF_LABS);

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(4, 28);
    u8g2.print("These features are");
    u8g2.setCursor(5, 40);
    u8g2.print("still experimental.");
    u8g2.setCursor(9, 52);
    u8g2.print("Try them out and");
    u8g2.setCursor(8, 64);
    u8g2.print("send us feedback!");
    u8g2.setFont(leaf_6x12);

    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 81;
    uint8_t menu_items_y[] = {190, 92};

    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
        case cursor_leaf_labs_thermal_track:
          menu_ui::printGlyph(settings.labs_thermalTrack ? menu_ui::ICON_ON : menu_ui::ICON_OFF);
          break;
        case cursor_leaf_labs_back:
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void LeafLabsMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_leaf_labs_thermal_track:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        setThermalTrackEnabled(!settings.labs_thermalTrack);
      }
      break;
    case cursor_leaf_labs_back:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        settingsMenuPage.backToSettingsMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        settingsMenuPage.quitMenu();
      }
      break;
  }
}
