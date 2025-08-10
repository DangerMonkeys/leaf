#include "ui/display/pages/primary/page_menu_main.h"

#include <Arduino.h>

#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

// cursor positions on the main menu
enum cursor_main_menu {
  cursor_back,
  cursor_altimeter,
  cursor_vario,
  cursor_display,
  cursor_units,
  cursor_gps,
  cursor_log,
  cursor_system
};

// all submenu pages and confirmation dialogs
enum display_menu_pages {
  page_menu_main,
  page_menu_altimeter,
  page_menu_vario,
  page_menu_display,
  page_menu_units,
  page_menu_gps,
  page_menu_log,
  page_menu_system,
  page_menu_resetConfirm,
};

// tracking which menu page we're on (we might move from the main menu page into a sub-meny)
uint8_t menu_page = page_menu_main;

void MainMenuPage::backToMainMenu() {
  cursor_position = cursor_back;
  menu_page = page_menu_main;
}

void MainMenuPage::quitMenu() {
  cursor_position = cursor_back;
  menu_page = page_menu_main;
  display.turnPage(PageAction::Back);
}

void MainMenuPage::draw() {
  switch (menu_page) {
    case page_menu_main:
      draw_main_menu();
      break;
    case page_menu_altimeter:
      altimeterMenuPage.draw();
      break;
    case page_menu_vario:
      varioMenuPage.draw();
      break;
    case page_menu_display:
      displayMenuPage.draw();
      break;
    case page_menu_units:
      unitsMenuPage.draw();
      break;
    case page_menu_gps:
      gpsMenuPage.draw();
      break;
    case page_menu_log:
      logMenuPage.draw();
      break;
    case page_menu_system:
      systemMenuPage.draw();
      break;
  }
}

void MainMenuPage::draw_main_menu() {
  u8g2.firstPage();
  do {
    // Title
    display_menuTitle("MAIN MENU");

    // Menu Items
    uint8_t start_y = 29;
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 72;
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 105, 120, 135};

    // first draw cursor selection box
    u8g2.drawRBox(setting_choice_x - 10, menu_items_y[cursor_position] - 14, 34, 16, 2);

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      u8g2.setCursor(setting_name_x, menu_items_y[i]);
      u8g2.print(labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      if (i == cursor_position)
        u8g2.setDrawColor(0);
      else
        u8g2.setDrawColor(1);
      if (i == cursor_back)
        u8g2.print((char)124);
      else
        u8g2.print((char)126);
      u8g2.setDrawColor(1);
    }
  } while (u8g2.nextPage());
}

void MainMenuPage::menu_item_action(Button button) {
  switch (cursor_position) {
    case cursor_back:
      if (button == Button::LEFT || button == Button::CENTER) {
        display.turnPage(PageAction::Back);
        speaker.playSound(fx::exit);
      } else if (button == Button::RIGHT) {
        // display_turnPage(page_next);  // maybe stop at menu, don't allow scrolling around back to
        // first page
      }
      break;
    case cursor_altimeter:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_altimeter;
      }
      break;
    case cursor_vario:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_vario;
      }
      break;
    case cursor_display:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_display;
      }
      break;
    case cursor_units:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_units;
      }
      break;
    case cursor_gps:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_gps;
      }
      break;
    case cursor_log:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_log;
      }
      break;
    case cursor_system:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_system;
      }
      break;
  }
}

bool MainMenuPage::mainMenuButtonEvent(Button button, ButtonState state, uint8_t count) {
  bool redraw = false;  // only redraw screen if a UI input changes something
  switch (button) {
    case Button::UP:
      if (state == RELEASED) {
        cursor_prev();
        redraw = true;
      }
      break;
    case Button::DOWN:
      if (state == RELEASED) {
        cursor_next();
        redraw = true;
      }
      break;
    case Button::LEFT:
    case Button::RIGHT:
    case Button::CENTER:
      if (state == RELEASED) {
        menu_item_action(button);
        redraw = true;
      }
      break;
  }
  return redraw;  // update display after button push so that the UI reflects any changes
                  // immediately
}

bool MainMenuPage::button_event(Button button, ButtonState state, uint8_t count) {
  bool redraw = false;  // only redraw screen if a UI input changes something
  switch (menu_page) {
    case page_menu_main:
      redraw = mainMenuButtonEvent(button, state, count);
      break;
    case page_menu_altimeter:
      redraw = altimeterMenuPage.button_event(button, state, count);
      break;
    case page_menu_vario:
      redraw = varioMenuPage.button_event(button, state, count);
      break;
    case page_menu_display:
      redraw = displayMenuPage.button_event(button, state, count);
      break;
    case page_menu_units:
      redraw = unitsMenuPage.button_event(button, state, count);
      break;
    case page_menu_gps:
      redraw = gpsMenuPage.button_event(button, state, count);
      break;
    case page_menu_log:
      redraw = logMenuPage.button_event(button, state, count);
      break;
    case page_menu_system:
      redraw = systemMenuPage.button_event(button, state, count);
      break;
  }
  return redraw;
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