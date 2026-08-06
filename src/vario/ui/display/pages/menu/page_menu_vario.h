#ifndef page_menu_units_h
#define page_menu_units_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class VarioMenuPage : public SettingsMenuPage {
 public:
  VarioMenuPage() {
    cursor_position = 0;
    cursor_max = 6;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);
  bool cursorUsesLeftButton() const override;

 private:
  static constexpr char* labels[7] = {
      "Back",
      "Beep Vol",
      /*"Tones",*/
      "QuietMode",
      "Sensitivity",
      /*"ClimbAvg",*/
      "ClimbStart",
      /*"LiftyAir",*/
      "SinkAlarm",
      "Vol Shortcut",
  };
};

#endif
