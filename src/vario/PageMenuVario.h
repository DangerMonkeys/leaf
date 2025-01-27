#ifndef page_menu_units_h
#define page_menu_units_h

#include <Arduino.h>

#include "buttons.h"
#include "menu_page.h"

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
      "Back",
      "BeepVolume",
      "QuietMode",
      "Sensitivity",
      "Tones",
      "LiftyAir",
      "ClimbAvg",
      "ClimbStart",
      "SinkAlarm",
  };
};

#endif