#include "menu_page.h"
#include "buttons.h"

void MenuPage::cursor_prev() {
  cursor_position--;
  if (cursor_position < 0) cursor_position = cursor_max;
}

void MenuPage::cursor_next() {
  cursor_position++;
  if (cursor_position > cursor_max) cursor_position = 0;
}

bool SettingsMenuPage::button_event(buttons button, button_states state, uint8_t count) {      
  switch (button) {
    case UP:
      if (state == RELEASED) cursor_prev();      
      break;
    case DOWN:
      if (state == RELEASED) cursor_next();      
      break;
    case LEFT:
      setting_change(LEFT, state, count);
      break;
    case RIGHT:
      setting_change(RIGHT, state, count);
      break;
    case CENTER:
      setting_change(CENTER, state, count);
      break;    
  }    
  bool redraw = false;
  if (button != NONE && state != NO_STATE) redraw = true;
  return redraw;   //update display after button push so that the UI reflects any changes immediately
}
