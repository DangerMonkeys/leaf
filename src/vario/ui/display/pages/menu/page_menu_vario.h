#ifndef page_menu_units_h
#define page_menu_units_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class VarioMenuPage : public SettingsMenuPage {
 public:
  VarioMenuPage() {
    cursor_position = 0;
    cursor_max = 8;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonState state, uint8_t count);

 private:
  static constexpr char* labels[9] = {
      "Back",     "Beep Vol",   "Tones",    "QuietMode", "Sensitivity",
      "ClimbAvg", "ClimbStart", "LiftyAir", "SinkAlarm",
  };
};

#endif