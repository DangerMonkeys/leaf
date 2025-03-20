#ifndef PageMenuAltimeter_h
#define PageMenuAltimeter_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

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
