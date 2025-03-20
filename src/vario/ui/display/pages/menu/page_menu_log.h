#ifndef PageMenuLog_h
#define PageMenuLog_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class LogMenuPage : public SettingsMenuPage {
 public:
  LogMenuPage() {
    cursor_position = 0;
    cursor_max = 4;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonState state, uint8_t count);

 private:
  static constexpr char* labels[8] = {"Back", "Format", "SaveLog", "AutoStart", "AutoStop"};
};

#endif