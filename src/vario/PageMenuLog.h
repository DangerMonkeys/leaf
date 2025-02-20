#ifndef PageMenuLog_h
#define PageMenuLog_h

#include <Arduino.h>

#include "buttons.h"
#include "menu_page.h"

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
  static constexpr char* labels[8] =
      {"Back", "Format", "SaveLog", "AutoStart", "AutoStop"};
};

#endif