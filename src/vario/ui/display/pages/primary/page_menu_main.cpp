#include "ui/display/pages/primary/page_menu_main.h"

#include <Arduino.h>

#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"
#include "ui/display/pages.h"
#include "ui/display/pages/menu/page_menu_wifi.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

// cursor positions on the main menu
enum cursor_main_menu {
  cursor_back,
  cursor_settings,
  cursor_flightTools,
  cursor_navData,
  cursor_gps,
  cursor_webApp,
  cursor_logbook,
  cursor_developer,
};

// all submenu pages and confirmation dialogs
enum display_menu_pages {
  page_menu_main,
  page_menu_settings,
  page_menu_flightTools,
  page_menu_navData,
  page_menu_gps,
  page_menu_developer,
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
  firstOpened = true;
#ifndef DEBUG_WIFI
  wifi_menu_ui::disconnectFromNetwork();
  Serial.println("WiFi disconnected");
#endif
  display.turnPage(PageAction::Back);
}

void MainMenuPage::draw() {
  switch (menu_page) {
    case page_menu_main:
      draw_main_menu();
      break;
    case page_menu_settings:
      settingsMenuPage.draw();
      break;
    case page_menu_flightTools:
      flightToolsMenuPage.draw();
      break;
    case page_menu_navData:
      navDataMenuPage.draw();
      break;
    case page_menu_gps:
      gpsMenuPage.draw();
      break;
    case page_menu_developer:
      developerMenuPage.draw();
  }
}

void MainMenuPage::draw_main_menu() {
  if (firstOpened) {
    firstOpened = false;
    wifi_menu_ui::attemptSavedNetworkConnection();
  }

  u8g2.firstPage();
  do {
    // Title
    menu_ui::drawTitle("Main Menu");
    wifi_menu_ui::drawStatusLine(29);

    // Menu Items
    uint8_t setting_name_x = 2;
    uint8_t setting_choice_x = 88;
    uint8_t menu_items_y[] = {190, 60, 75, 90, 105, 120, 135, 150};

    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {
      if (row_hidden(i)) continue;

      const bool selected = i == cursor_position;
      menu_ui::beginRow(menu_items_y[i], selected);
      menu_ui::drawLabel(setting_name_x, menu_items_y[i], labels[i], glyphs[i]);
      if (i == cursor_back) {
        menu_ui::drawBackIcon(setting_choice_x, menu_items_y[i]);
      } else {
        menu_ui::drawEnterIcon(setting_choice_x, menu_items_y[i], selected);
      }
      menu_ui::endRow();
    }
  } while (u8g2.nextPage());
}

void MainMenuPage::menu_item_action(Button button) {
  switch (cursor_position) {
    case cursor_back:
      if (button == Button::LEFT || button == Button::CENTER) {
        speaker.playSound(fx::exit);
        quitMenu();
      } else if (button == Button::RIGHT) {
        // display_turnPage(page_next);  // maybe stop at menu, don't allow scrolling around back
        // to first page
      }
      break;
    case cursor_settings:
      if (button == Button::RIGHT || button == Button::CENTER) {
        settingsMenuPage.backToSettingsMenu();
        menu_page = page_menu_settings;
      }
      break;
    case cursor_flightTools:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_flightTools;
      }
      break;
    case cursor_navData:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_navData;
      }
      break;
    case cursor_gps:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_gps;
      }
      break;
    case cursor_webApp:
      if (button == Button::RIGHT || button == Button::CENTER) {
        wifiMenuPage.showWebApp();
      }
      break;
    case cursor_logbook:
      if (button == Button::RIGHT || button == Button::CENTER) {
        logMenuPage.showLogbook();
      }
      break;
    case cursor_developer:
      if (button == Button::RIGHT || button == Button::CENTER) {
        menu_page = page_menu_developer;
      }
      break;
  }
}

bool MainMenuPage::row_hidden(uint8_t row) const {
  if (row == cursor_developer && !settings.dev_mode) return true;

  return false;
}

void MainMenuPage::skip_hidden_forward() {
  while (row_hidden(cursor_position)) cursor_next();
}

void MainMenuPage::skip_hidden_backward() {
  while (row_hidden(cursor_position)) cursor_prev();
}

bool MainMenuPage::mainMenuButtonEvent(Button button, ButtonEvent state, uint8_t count) {
  bool redraw = false;  // only redraw screen if a UI input changes something
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
      if (state == ButtonEvent::CLICKED) {
        menu_item_action(button);
        redraw = true;
      }
      break;
  }
  return redraw;  // update display after button push so that the UI reflects any changes
                  // immediately
}

bool MainMenuPage::button_event(Button button, ButtonEvent state, uint8_t count) {
  bool redraw = false;  // only redraw screen if a UI input changes something
  switch (menu_page) {
    case page_menu_main:
      redraw = mainMenuButtonEvent(button, state, count);
      break;
    case page_menu_settings:
      redraw = settingsMenuPage.button_event(button, state, count);
      break;
    case page_menu_flightTools:
      redraw = flightToolsMenuPage.button_event(button, state, count);
      break;
    case page_menu_navData:
      redraw = navDataMenuPage.button_event(button, state, count);
      break;
    case page_menu_gps:
      redraw = gpsMenuPage.button_event(button, state, count);
      break;
    case page_menu_developer:
      redraw = developerMenuPage.button_event(button, state, count);
      break;
  }
  return redraw;
}
