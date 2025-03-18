#ifndef PageMenuDisplay_h
#define PageMenuDisplay_h

#include <Arduino.h>

#include "buttons.h"
#include "menu_page.h"

class DisplayMenuPage : public SettingsMenuPage {
 public:
  DisplayMenuPage() {
    cursor_position = 0;
    cursor_max = 5;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonState state, uint8_t count);

 private:
  static constexpr char* labels[6] = {"Back",        "Debug",    "Thermal",
                                      "Thermal ADV", "Navigate", "Contrast"};
};

#endif