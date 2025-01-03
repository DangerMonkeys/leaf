#ifndef PageMenuAltimeter_h
#define PageMenuAltimeter_h

#include <Arduino.h>

#include "buttons.h"
#include "menu_page.h"

class AltimeterMenuPage : public SettingsMenuPage {
 public:
  AltimeterMenuPage() {
    cursor_position = 0;
    cursor_max = 3;
  }
  void draw();

 protected:
  void setting_change(Button button, ButtonState state, uint8_t count);
};

#endif
