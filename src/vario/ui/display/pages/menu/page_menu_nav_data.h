#ifndef PageMenuNavData_h
#define PageMenuNavData_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class NavDataMenuPage : public SettingsMenuPage {
 public:
  NavDataMenuPage() {
    cursor_position = 0;
    cursor_max = 5;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);
};

#endif
