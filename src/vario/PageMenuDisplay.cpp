#include "PageMenuDisplay.h"

#include <Arduino.h>

#include "buttons.h"
#include "display.h"
#include "displayFields.h"
#include "fonts.h"
#include "pages.h"
#include "settings.h"
#include "speaker.h"

enum display_menu_items {
  cursor_display_back,
  cursor_display_show_debug,
  cursor_display_show_thrm_sim,
  cursor_display_show_thrm_adv,
  cursor_display_show_nav,
  cursor_display_contrast,
};

void DisplayMenuPage::draw() {
  u8g2.firstPage();
  do {
    // Title
    display_menuTitle("DISPLAY");

    // Menu Items
    u8g2.setCursor(0, 45);
    u8g2.print("Show Pages:");

    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 3;
    uint8_t setting_choice_x = 78;
    uint8_t menu_items_y[] = {190, 60, 75, 90, 105, 135};

    // first draw cursor selection box
    u8g2.drawRBox(setting_choice_x - 2, menu_items_y[cursor_position] - 14, 22, 16, 2);

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      u8g2.setCursor(setting_name_x, menu_items_y[i]);
      u8g2.print(labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      if (i == cursor_position)
        u8g2.setDrawColor(0);
      else
        u8g2.setDrawColor(1);
      switch (i) {
        case cursor_display_show_debug:
          if (SHOW_DEBUG)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_display_show_thrm_sim:
          if (SHOW_THRM_SIMP)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_display_show_thrm_adv:
          if (SHOW_THRM_ADV)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_display_show_nav:
          if (SHOW_NAV)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_display_contrast:
          if (CONTRAST < 10) u8g2.print(" ");
          u8g2.print(CONTRAST);
          break;
        case cursor_display_back:
          u8g2.print((char)124);
          break;
      }
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void DisplayMenuPage::setting_change(Button dir, ButtonState state, uint8_t count) {
  switch (cursor_position) {
    case cursor_display_show_debug:
      if (state == RELEASED && dir == Button::CENTER) settings_toggleBoolOnOff(&SHOW_DEBUG);
      break;
    case cursor_display_show_thrm_sim:
      if (state == RELEASED && dir == Button::CENTER) settings_toggleBoolOnOff(&SHOW_THRM_SIMP);
      break;
    case cursor_display_show_thrm_adv:
      if (state == RELEASED && dir == Button::CENTER) settings_toggleBoolOnOff(&SHOW_THRM_ADV);
      break;
    case cursor_display_show_nav:
      if (state == RELEASED && dir == Button::CENTER) settings_toggleBoolOnOff(&SHOW_NAV);
      break;
    case cursor_display_contrast:
      if (state == RELEASED && dir != Button::NONE)
        settings_adjustContrast(dir);
      else if (state == HELD && dir == Button::NONE)
        settings_adjustContrast(dir);
      break;
    case cursor_display_back:
      if (state == RELEASED) {
        speaker_playSound(fx_cancel);
        settings_save();
        mainMenuPage.backToMainMenu();
      } else if (state == HELD) {
        speaker_playSound(fx_exit);
        settings_save();
        mainMenuPage.quitMenu();
      }
  }
}

// helpful switch constructors to copy-paste as needed:
/*
switch (button) {
  case Button::UP:
    break;
  case Button::DOWN:
    break;
  case Button::LEFT:
    break;
  case Button::RIGHT:
    break;
  case Button::CENTER:
    break;
*/

/*
switch (state) {
  case RELEASED:
    break;
  case PRESSED:
    break;
  case HELD:
    break;
  case HELD_LONG:
    break;
}
*/