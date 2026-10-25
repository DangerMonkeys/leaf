#include "ui/display/pages/menu/page_menu_leaf_labs.h"

#include <Arduino.h>

#include "page_menu_training.h"
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
  cursor_leaf_labs_leaf_log,
#ifdef LEAF_TRAINING_STUDENT
  cursor_leaf_labs_training,
#endif
};

namespace {
  void setLeafLogEnabled(bool enabled) {
    if (settings.labs_leafLog == enabled) return;

    settings.labs_leafLog = enabled;
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
#ifdef LEAF_TRAINING_STUDENT
    uint8_t menu_items_y[] = {190, 86, 106};
#else
    uint8_t menu_items_y[] = {190, 86};
#endif

    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
        case cursor_leaf_labs_leaf_log:
          menu_ui::printGlyph(settings.labs_leafLog ? menu_ui::ICON_ON : menu_ui::ICON_OFF);
          break;
#ifdef LEAF_TRAINING_STUDENT
        case cursor_leaf_labs_training:
          menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
          break;
#endif
        case cursor_leaf_labs_back:
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
      }
      menu_ui::endRow();
    }

    if (cursor_position == cursor_leaf_labs_leaf_log) {
      const char* const noteLines[] = {
          "Shows Leaf Log",
          "linking in the",
          "Leaf web app",
      };
#ifdef LEAF_TRAINING_STUDENT
      menu_ui::drawNoteBox(112, noteLines, 3);
#else
      menu_ui::drawNoteBox(menu_items_y[cursor_leaf_labs_leaf_log], noteLines, 3);
#endif
    }
  } while (u8g2.nextPage());
}

void LeafLabsMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_leaf_labs_leaf_log:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        setLeafLogEnabled(!settings.labs_leafLog);
      }
      break;
#ifdef LEAF_TRAINING_STUDENT
    case cursor_leaf_labs_training:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT)) {
        speaker.playSound(fx::increase);
        TrainingMenuPage::show();
      }
      break;
#endif
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
