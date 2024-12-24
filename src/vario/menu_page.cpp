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

bool SettingsMenuPage::button_event(Button button, ButtonState state, uint8_t count) {      
  switch (button) {
    case Button::UP:
      if (state == RELEASED) cursor_prev();      
      break;
    case Button::DOWN:
      if (state == RELEASED) cursor_next();      
      break;
    case Button::LEFT:
      setting_change(Button::LEFT, state, count);
      break;
    case Button::RIGHT:
      setting_change(Button::RIGHT, state, count);
      break;
    case Button::CENTER:
      setting_change(Button::CENTER, state, count);
      break;    
  }    
  bool redraw = false;
  if (button != Button::NONE && state != NO_STATE) redraw = true;
  return redraw;   //update display after button push so that the UI reflects any changes immediately
}
