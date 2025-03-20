#include "ui/display/pages/menu/page_menu_display.h"

#include <Arduino.h>

#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

enum display_menu_items {
  cursor_display_back,
  cursor_display_show_debug,
  cursor_display_show_thrm,
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
          if (settings.disp_showDebugPage)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_display_show_thrm:
          if (settings.disp_showThmPage)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_display_show_thrm_adv:
          if (settings.disp_showThmAdvPage)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_display_show_nav:
          if (settings.disp_showNavPage)
            u8g2.print(char(125));
          else
            u8g2.print(char(123));
          break;
        case cursor_display_contrast:
          if (settings.disp_contrast < 10) u8g2.print(" ");
          u8g2.print(settings.disp_contrast);
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
      if (state == RELEASED && dir == Button::CENTER)
        settings.toggleBoolOnOff(&settings.disp_showDebugPage);
      break;
    case cursor_display_show_thrm:
      if (state == RELEASED && dir == Button::CENTER)
        settings.toggleBoolOnOff(&settings.disp_showThmPage);
      break;
    case cursor_display_show_thrm_adv:
      if (state == RELEASED && dir == Button::CENTER)
        settings.toggleBoolOnOff(&settings.disp_showThmAdvPage);
      break;
    case cursor_display_show_nav:
      if (state == RELEASED && dir == Button::CENTER)
        settings.toggleBoolOnOff(&settings.disp_showNavPage);
      break;
    case cursor_display_contrast:
      if (state == RELEASED && dir != Button::NONE)
        settings.adjustContrast(dir);
      else if (state == HELD && dir == Button::NONE)
        settings.adjustContrast(dir);
      break;
    case cursor_display_back:
      if (state == RELEASED) {
        speaker_playSound(fx_cancel);
        settings.save();
        mainMenuPage.backToMainMenu();
      } else if (state == HELD) {
        speaker_playSound(fx_exit);
        settings.save();
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