#include <Arduino.h>
#include "page_menu_units.h"
#include "buttons.h"
#include "display.h"
#include "fonts.h"
#include "settings.h"

char * labels[] = {
  "Back",
  "Alt:",
  "Climb:",
  "Speed:",
  "Dist:",
  "Head:",
  "Temp:",
  "Time:"
};

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

int8_t cursor_position = 0;   // 0 means nothing selected
uint8_t cursor_max = 7;       // the number of items (0-based) in the enum list above


void UnitsMenuPage::draw() {
  u8g2.firstPage();
  do { 
    // Title(s) 
    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(2, 12);
    u8g2.setDrawColor(1);
    u8g2.print("UNITS");
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
          if (UNITS_hours) u8g2.print("12h");
          else u8g2.print("24h");
          break;
        case cursor_units_back:
          u8g2.print("<-");
          break;        
      }
    u8g2.setDrawColor(1);
    }
  } while ( u8g2.nextPage() ); 
}


void cursor_prev() {
  cursor_position--;
  if (cursor_position < 0) cursor_position = cursor_max;
}

void cursor_next() {
cursor_position++;
  if (cursor_position > cursor_max) cursor_position = 0;
}

void setting_change(int8_t dir) {
  switch (cursor_position) {
    case cursor_units_alt:
      settings_toggleUnits(&UNITS_alt);
      break;
    case cursor_units_climb:
      settings_toggleUnits(&UNITS_climb);
      break;
    case cursor_units_speed:
      settings_toggleUnits(&UNITS_speed);
      break;
    case cursor_units_distance:
      settings_toggleUnits(&UNITS_distance);
      break;
    case cursor_units_heading:
      settings_toggleUnits(&UNITS_heading);
      break;
    case cursor_units_temp:
      settings_toggleUnits(&UNITS_temp);
      break;
    case cursor_units_hours:
      settings_toggleUnits(&UNITS_hours);
      break;
    case cursor_units_back:
      //if (dir == 0) 
      display_turnPage(page_back);
      break;
  }
}

bool UnitsMenuPage::button_event(uint8_t button, uint8_t state, uint8_t count) {    
  switch (button) {
    case UP:
      if (state == RELEASED) cursor_prev();
      break;
    case DOWN:
      if (state == RELEASED) cursor_next();
      break;
    case LEFT:
      if (state == RELEASED) setting_change(-1);
      break;
    case RIGHT:
      if (state == RELEASED) setting_change(1);
      break;
    case CENTER:
      if (state == RELEASED) setting_change(0);
      break;    
  }    
  return true;   //update display after button push so that the UI reflects any changes immediately
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