#include "ui/display/pages/menu/page_menu_display.h"

#include <Arduino.h>

#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum display_menu_items {
  cursor_display_back,
  cursor_display_show_simple,  // basic page
  cursor_display_show_thrm,    // user page
  // cursor_display_show_thrm_adv,  // currently not used and half-developed
  cursor_display_show_thermal_nav,  // thermal nav page
  cursor_display_show_nav,          // waypoint nav page
  cursor_display_contrast,
};

namespace {
  uint8_t enabledPrimaryPageCount() {
    uint8_t count = 0;
    if (settings.disp_showSimplePage) count++;
    if (settings.disp_showThmPage) count++;
    if (settings.disp_showThmAdvPage) count++;
    if (settings.disp_showThermalNavPage) count++;
    if (settings.disp_showNavPage) count++;
    return count;
  }

  void togglePrimaryPageSetting(bool* showPage) {
    if (*showPage && enabledPrimaryPageCount() <= 1) {
      speaker.playSound(fx::bad);
      return;
    }
    settings.toggleBoolOnOff(showPage);
  }
}  // namespace

void DisplayMenuPage::draw() {
  display.ensurePrimaryPageEnabled();

  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("Display", menu_ui::GLYPH_DISPLAY);

    // Menu Items
    u8g2.setCursor(0, 45);
    u8g2.print("Show Pages:");

    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 3;
    uint8_t setting_choice_x = 78;
    uint8_t menu_items_y[] = {190, 60, 75, 90, 105, 135};

    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
        case cursor_display_show_simple:
          if (settings.disp_showSimplePage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_display_show_thrm:
          if (settings.disp_showThmPage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
          /*
          case cursor_display_show_thrm_adv:
            if (settings.disp_showThmAdvPage)
              u8g2.print(char(125));
            else
              u8g2.print(char(123));
            break;
          */
        case cursor_display_show_thermal_nav:
          if (settings.disp_showThermalNavPage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_display_show_nav:
          if (settings.disp_showNavPage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_display_contrast:
          if (settings.disp_contrast < 10) u8g2.print(" ");
          u8g2.print(settings.disp_contrast);
          break;
        case cursor_display_back:
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void DisplayMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_display_show_simple:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        togglePrimaryPageSetting(&settings.disp_showSimplePage);
      break;
    case cursor_display_show_thrm:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        togglePrimaryPageSetting(&settings.disp_showThmPage);
      break;
    case cursor_display_show_nav:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        togglePrimaryPageSetting(&settings.disp_showNavPage);
      break;
    case cursor_display_show_thermal_nav:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        togglePrimaryPageSetting(&settings.disp_showThermalNavPage);
      break;
    case cursor_display_contrast:
      if (state == ButtonEvent::CLICKED || state == ButtonEvent::INCREMENTED)
        settings.adjustContrast(dir);
      break;
    case cursor_display_back:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        settingsMenuPage.backToSettingsMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        settingsMenuPage.quitMenu();
      }
  }
}

bool DisplayMenuPage::cursorUsesLeftButton() const {
  return cursor_position == cursor_display_contrast;
}
