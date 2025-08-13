#ifndef PageMenuDeveloper_h
#define PageMenuDeveloper_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class DeveloperMenuPage : public SettingsMenuPage {
 public:
  DeveloperMenuPage() {
    cursor_position = 0;
    cursor_max = 3;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonState state, uint8_t count);

 private:
  static constexpr char* labels[8] = {"Back", "Start Log", "HW-Discon", "Log Now:"};
};

#endif