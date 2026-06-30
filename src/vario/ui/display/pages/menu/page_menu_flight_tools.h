#ifndef PageMenuFlightTools_h
#define PageMenuFlightTools_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class FlightToolsMenuPage : public SettingsMenuPage {
 public:
  FlightToolsMenuPage() {
    cursor_position = 0;
    cursor_max = 3;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);
};

#endif
