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
  cursor_display_show_basic,
  cursor_display_show_user,
  cursor_display_show_thermal_core,
  cursor_display_show_thermal_track,
  cursor_display_show_navigate,
  cursor_display_contrast,
};

namespace {
  uint8_t enabledPrimaryPageCount() {
    uint8_t count = 0;
    if (settings.disp_showBasicPage) count++;
    if (settings.disp_showUserPage) count++;
    if (settings.labs_thermalCore && settings.disp_showThermalCorePage) count++;
    if (settings.labs_thermalTrack && settings.disp_showThermalTrackPage) count++;
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
  if (row_hidden(cursor_position)) skip_hidden_forward();

  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("Display", menu_ui::GLYPH_DISPLAY);

    // Menu Items
    u8g2.setCursor(0, 45);
    u8g2.print("Show Pages:");

    uint8_t setting_name_x = 3;
    uint8_t setting_choice_x = 81;
    uint8_t contrast_value_x = 77;

    for (int i = 0; i <= cursor_max; i++) {
      if (row_hidden(i)) continue;

      const uint8_t y = row_y(i);
      const bool selected = i == cursor_position;
      menu_ui::beginRow(y, selected);
      menu_ui::drawLabel(setting_name_x, y, labels[i]);
      u8g2.setCursor(setting_choice_x, y);
      switch (i) {
        case cursor_display_show_basic:
          if (settings.disp_showBasicPage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_display_show_user:
          if (settings.disp_showUserPage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_display_show_thermal_core:
          if (settings.disp_showThermalCorePage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_display_show_thermal_track:
          if (settings.disp_showThermalTrackPage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_display_show_navigate:
          if (settings.disp_showNavPage)
            menu_ui::printGlyph(menu_ui::ICON_ON);
          else
            menu_ui::printGlyph(menu_ui::ICON_OFF);
          break;
        case cursor_display_contrast:
          u8g2.setCursor(contrast_value_x, y);
          if (settings.disp_contrast < 10) u8g2.print(" ");
          u8g2.print(settings.disp_contrast);
          break;
        case cursor_display_back:
          menu_ui::drawBackIcon(setting_choice_x, y);
          break;
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

bool DisplayMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
  bool redraw = false;
  switch (button) {
    case Button::UP:
      if (state == ButtonEvent::CLICKED) {
        cursor_prev();
        skip_hidden_backward();
        playCursorMoveSound();
        redraw = true;
      }
      break;
    case Button::DOWN:
      if (state == ButtonEvent::CLICKED) {
        cursor_next();
        skip_hidden_forward();
        playCursorMoveSound();
        redraw = true;
      }
      break;
    case Button::LEFT:
      if (state == ButtonEvent::CLICKED && cursor_position != CURSOR_BACK &&
          !cursorUsesLeftButton()) {
        leftButtonBackShortcut(state, count);
      } else {
        setting_change(Button::LEFT, state, count);
      }
      redraw = true;
      break;
    case Button::RIGHT:
      setting_change(Button::RIGHT, state, count);
      redraw = true;
      break;
    case Button::CENTER:
      setting_change(Button::CENTER, state, count);
      redraw = true;
      break;
    case Button::NONE:
      break;
  }
  return redraw;
}

void DisplayMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_display_show_basic:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        togglePrimaryPageSetting(&settings.disp_showBasicPage);
      break;
    case cursor_display_show_user:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        togglePrimaryPageSetting(&settings.disp_showUserPage);
      break;
    case cursor_display_show_navigate:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        togglePrimaryPageSetting(&settings.disp_showNavPage);
      break;
    case cursor_display_show_thermal_core:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        togglePrimaryPageSetting(&settings.disp_showThermalCorePage);
      break;
    case cursor_display_show_thermal_track:
      if (state == ButtonEvent::CLICKED && (dir == Button::CENTER || dir == Button::RIGHT))
        togglePrimaryPageSetting(&settings.disp_showThermalTrackPage);
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

bool DisplayMenuPage::row_hidden(uint8_t row) const {
  return (row == cursor_display_show_thermal_core && !settings.labs_thermalCore) ||
         (row == cursor_display_show_thermal_track && !settings.labs_thermalTrack);
}

uint8_t DisplayMenuPage::row_y(uint8_t row) const {
  if (row == cursor_display_back) return 190;

  uint8_t y = 60;
  for (uint8_t i = 1; i < row; ++i) {
    if (!row_hidden(i)) y += 15;
  }
  if (row == cursor_display_contrast) y += 15;
  return y;
}

void DisplayMenuPage::skip_hidden_forward() {
  while (row_hidden(cursor_position)) cursor_next();
}

void DisplayMenuPage::skip_hidden_backward() {
  while (row_hidden(cursor_position)) cursor_prev();
}
