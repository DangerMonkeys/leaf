#include "ui/display/pages/menu/page_menu_settings.h"

#include <Arduino.h>

#include "comms/fanet_radio.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/display/pages/fanet/page_fanet.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum settings_menu_items {
  cursor_settings_back,
  cursor_settings_vario,
  cursor_settings_altimeter,
  cursor_settings_display,
  cursor_settings_units,
  cursor_settings_logging,
  cursor_settings_connect,
  cursor_settings_system,
  cursor_settings_fanet,
};

enum settings_menu_pages {
  page_menu_settings_root,
  page_menu_settings_vario,
  page_menu_settings_altimeter,
  page_menu_settings_display,
  page_menu_settings_units,
  page_menu_settings_logging,
  page_menu_settings_connect,
  page_menu_settings_system,
};

uint8_t settings_menu_page = page_menu_settings_root;

namespace {
  constexpr char* labels[9] = {"Back",    "Vario",   "Altimeter", "Display", "Units",
                               "Logging", "Connect", "System",    "Fanet"};

  constexpr uint8_t glyphs[9] = {0,
                                 menu_ui::GLYPH_VARIO,
                                 menu_ui::GLYPH_ALTIMETER,
                                 menu_ui::GLYPH_DISPLAY,
                                 menu_ui::GLYPH_UNITS,
                                 menu_ui::GLYPH_LOGGING,
                                 menu_ui::GLYPH_CONNECTIVITY,
                                 menu_ui::GLYPH_SETTINGS,
                                 menu_ui::GLYPH_FANET};
}  // namespace

void SettingsRootMenuPage::backToSettingsMenu() {
  cursor_position = cursor_settings_back;
  settings_menu_page = page_menu_settings_root;
}

void SettingsRootMenuPage::focusSystemUpdate() {
  settings_menu_page = page_menu_settings_system;
  systemMenuPage.focusUpdate();
}

bool SettingsRootMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
  switch (settings_menu_page) {
    case page_menu_settings_root:
      return settingsRootButtonEvent(button, state, count);
    case page_menu_settings_vario:
      return varioMenuPage.button_event(button, state, count);
    case page_menu_settings_altimeter:
      return altimeterMenuPage.button_event(button, state, count);
    case page_menu_settings_display:
      return displayMenuPage.button_event(button, state, count);
    case page_menu_settings_units:
      return unitsMenuPage.button_event(button, state, count);
    case page_menu_settings_logging:
      return logMenuPage.button_event(button, state, count);
    case page_menu_settings_connect:
      return wifiMenuPage.button_event(button, state, count);
    case page_menu_settings_system:
      return systemMenuPage.button_event(button, state, count);
  }
  return false;
}

void SettingsRootMenuPage::draw() {
  switch (settings_menu_page) {
    case page_menu_settings_root:
      drawSettingsMenu();
      break;
    case page_menu_settings_vario:
      varioMenuPage.draw();
      break;
    case page_menu_settings_altimeter:
      altimeterMenuPage.draw();
      break;
    case page_menu_settings_display:
      displayMenuPage.draw();
      break;
    case page_menu_settings_units:
      unitsMenuPage.draw();
      break;
    case page_menu_settings_logging:
      logMenuPage.draw();
      break;
    case page_menu_settings_connect:
      wifiMenuPage.draw();
      break;
    case page_menu_settings_system:
      systemMenuPage.draw();
      break;
  }
}

void SettingsRootMenuPage::drawSettingsMenu() {
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Settings", menu_ui::GLYPH_SETTINGS);

    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 76;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 105, 120, 135, 150};

    for (int i = 0; i <= cursor_max; i++) {
      if (row_hidden(i)) continue;

      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i], glyphs[i]);
      if (i == cursor_settings_back) {
        menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
      } else {
        menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void SettingsRootMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (state != ButtonEvent::CLICKED && state != ButtonEvent::HELD) return;

  switch (cursor_position) {
    case cursor_settings_back:
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
    case cursor_settings_vario:
      if (state == ButtonEvent::CLICKED) settings_menu_page = page_menu_settings_vario;
      break;
    case cursor_settings_altimeter:
      if (state == ButtonEvent::CLICKED) settings_menu_page = page_menu_settings_altimeter;
      break;
    case cursor_settings_display:
      if (state == ButtonEvent::CLICKED) settings_menu_page = page_menu_settings_display;
      break;
    case cursor_settings_units:
      if (state == ButtonEvent::CLICKED) settings_menu_page = page_menu_settings_units;
      break;
    case cursor_settings_logging:
      if (state == ButtonEvent::CLICKED) settings_menu_page = page_menu_settings_logging;
      break;
    case cursor_settings_connect:
      if (state == ButtonEvent::CLICKED) settings_menu_page = page_menu_settings_connect;
      break;
    case cursor_settings_system:
      if (state == ButtonEvent::CLICKED) settings_menu_page = page_menu_settings_system;
      break;
    case cursor_settings_fanet:
      if (state == ButtonEvent::CLICKED) PageFanet::show();
      break;
  }
}

bool SettingsRootMenuPage::row_hidden(uint8_t row) const {
  if (row == cursor_settings_fanet) {
#ifndef FANET_CAPABLE
    return true;
#else
    return fanetRadio.getState() == FanetRadioState::UNINSTALLED;
#endif
  }

  return false;
}

void SettingsRootMenuPage::skip_hidden_forward() {
  while (row_hidden(cursor_position)) cursor_next();
}

void SettingsRootMenuPage::skip_hidden_backward() {
  while (row_hidden(cursor_position)) cursor_prev();
}

bool SettingsRootMenuPage::settingsRootButtonEvent(Button button, ButtonEvent state,
                                                   uint8_t count) {
  bool redraw = false;
  switch (button) {
    case Button::UP:
      if (state == ButtonEvent::CLICKED) {
        cursor_prev();
        skip_hidden_backward();
        redraw = true;
      }
      break;
    case Button::DOWN:
      if (state == ButtonEvent::CLICKED) {
        cursor_next();
        skip_hidden_forward();
        redraw = true;
      }
      break;
    case Button::LEFT:
    case Button::RIGHT:
    case Button::CENTER:
      if (state == ButtonEvent::CLICKED || state == ButtonEvent::HELD) {
        setting_change(button, state, count);
        redraw = true;
      }
      break;
  }
  return redraw;
}
