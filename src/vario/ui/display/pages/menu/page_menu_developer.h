#ifndef PageMenuDeveloper_h
#define PageMenuDeveloper_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class DeveloperMenuPage : public SettingsMenuPage {
 public:
  DeveloperMenuPage() {
    cursor_position = 0;
    cursor_max = 4;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  static constexpr char* labels[8] = {"Back", "Fanet ReTX", "StartBusLog", "Detach HW", "Log Now:"};
};

#endif