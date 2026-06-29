#include "ui/display/pages/menu/page_menu_units.h"

#include <Arduino.h>

#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum units_menu_items {
  cursor_units_back,
  cursor_units_alt,
  cursor_units_climb,
  cursor_units_speed,
  cursor_units_distance,
  cursor_units_heading,
  cursor_units_temp,
  cursor_units_hours

};

void UnitsMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("Units", menu_ui::GLYPH_UNITS);

    // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 68;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 105, 120, 135};

    for (int i = 0; i <= cursor_max; i++) {
      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      switch (i) {
        case cursor_units_alt:
          if (settings.units_alt)
            u8g2.print("ft");
          else
            u8g2.print(" m");
          break;
        case cursor_units_climb:
          if (settings.units_climb)
            u8g2.print("fpm");
          else
            u8g2.print("m/s");
          break;
        case cursor_units_speed:
          if (settings.units_speed)
            u8g2.print("mph");
          else
            u8g2.print("kph");
          break;
        case cursor_units_distance:
          if (settings.units_distance)
            u8g2.print("mi");
          else
            u8g2.print("km");
          break;
        case cursor_units_heading:
          if (settings.units_heading)
            u8g2.print("NNW");
          else
            u8g2.print("deg");
          break;
        case cursor_units_temp:
          if (settings.units_temp)
            u8g2.print("F");
          else
            u8g2.print("C");
          break;
        case cursor_units_hours:
          if (settings.units_hours)
            u8g2.print("12h");
          else
            u8g2.print("24h");
          break;
        case cursor_units_back:
          menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
          break;
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void UnitsMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  switch (cursor_position) {
    case cursor_units_alt:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolNeutral(&settings.units_alt);
      break;
    case cursor_units_climb:
      if (state == ButtonEvent::CLICKED) {
        settings.toggleBoolNeutral(&settings.units_climb);  // change climb units as user reqested
        settings.adjustSinkAlarmUnits(
            settings.units_climb);  // and change sink-alarm units to match
      }
      break;
    case cursor_units_speed:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolNeutral(&settings.units_speed);
      break;
    case cursor_units_distance:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolNeutral(&settings.units_distance);
      break;
    case cursor_units_heading:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolNeutral(&settings.units_heading);
      break;
    case cursor_units_temp:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolNeutral(&settings.units_temp);
      break;
    case cursor_units_hours:
      if (state == ButtonEvent::CLICKED) settings.toggleBoolNeutral(&settings.units_hours);
      break;
    case cursor_units_back:
      if (state == ButtonEvent::CLICKED) {
        speaker.playSound(fx::cancel);
        settings.save();
        settingsMenuPage.backToSettingsMenu();
      } else if (state == ButtonEvent::HELD) {
        speaker.playSound(fx::exit);
        settings.save();
        mainMenuPage.quitMenu();
      }
      break;
  }
}
