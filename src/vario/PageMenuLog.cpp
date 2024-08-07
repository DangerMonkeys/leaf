#include <Arduino.h>
#include "PageMenuLog.h"
#include "pages.h"

#include "buttons.h"
#include "display.h"
#include "fonts.h"
#include "settings.h"
#include "speaker.h"
#include "log.h"


enum log_menu_items { 
  cursor_units_back,
  cursor_units_alt,
  cursor_units_climb,
  cursor_units_speed,
  cursor_units_distance,
  cursor_units_heading,
  cursor_units_temp,
  cursor_units_hours

};


void LogMenuPage::draw() {
  u8g2.firstPage();
  do { 
    // Title(s) 
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(2, 12);
    u8g2.setDrawColor(1);
    u8g2.print("LOG");
    u8g2.drawHLine(0, 15, 64);

  // Menu Items
    uint8_t start_y = 29;
    uint8_t y_spacing = 16;
    uint8_t setting_name_x = 3;
    uint8_t setting_choice_x = 44;    
    uint8_t menu_items_y[] = {190, 45, 60, 75, 90, 105, 120, 135};

    //first draw cursor selection box
    u8g2.drawRBox(setting_choice_x-2, menu_items_y[cursor_position]-14, 22, 16, 2);
    
    // then draw all the menu items
    for (int i = 0; i <= cursor_max; i++) {      
      u8g2.setCursor(setting_name_x, menu_items_y[i]);
      u8g2.print(labels[i]);
      u8g2.setCursor(setting_choice_x, menu_items_y[i]);
      if (i == cursor_position) u8g2.setDrawColor(0);
      else u8g2.setDrawColor(1);
      switch (i) {
        case cursor_units_alt:
          if (UNITS_alt) u8g2.print("ft");
          else u8g2.print(" m");
          break;
        case cursor_units_climb:
          if (UNITS_climb) u8g2.print("fpm");
          else u8g2.print("m/s");    
          break;
        case cursor_units_speed:
          if (UNITS_speed) u8g2.print("mph");
          else u8g2.print("kph");
          break;
        case cursor_units_distance:
          if (UNITS_distance) u8g2.print("mi");
          else u8g2.print("km");
          break;
        case cursor_units_heading:
          if (UNITS_heading) u8g2.print("NNW");
          else u8g2.print("deg");
          break;
        case cursor_units_temp:
          if (UNITS_temp) u8g2.print("F");
          else u8g2.print("C");
          break;
        case cursor_units_hours:
          if (flightTimer_isRunning()) u8g2.print("STOP");
          else u8g2.print("START");
          break;
        case cursor_units_back:
          u8g2.print((char)124);
          break;        
      }
    u8g2.setDrawColor(1);
    }
    display_flightTimer(4, 170, false);
  } while ( u8g2.nextPage() ); 
}


void LogMenuPage::setting_change(int8_t dir, uint8_t state, uint8_t count) {
  switch (cursor_position) {
    case cursor_units_alt:

      break;
    case cursor_units_climb:

      break;
    case cursor_units_speed:

      break;
    case cursor_units_distance:

      break;
    case cursor_units_heading:

      break;
    case cursor_units_temp:

      break;
    case cursor_units_hours:
      if (dir == 0) {
        if (state == RELEASED) flightTimer_toggle();
        else if (state == HELD) flightTimer_reset();
      }
      break;
    case cursor_units_back:
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
  case UP:
    break;
  case DOWN:
    break;
  case LEFT:
    break;
  case RIGHT:
    break;
  case CENTER:
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