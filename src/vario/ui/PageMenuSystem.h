#ifndef PageMenuSystem_h
#define PageMenuSystem_h

#include <Arduino.h>

#include "PageMenuSystemWifi.h"
#include "buttons.h"
#include "menu_page.h"

class SystemMenuPage : public SettingsMenuPage {
 public:
  SystemMenuPage() {
    cursor_position = 0;
    cursor_max = 9;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonState state, uint8_t count);

 private:
  static constexpr char* labels[10] = {"Back",  "TimeZone", "Volume", "Auto-Off", "ShowWarning",
                                       "Fanet", "Wifi",     "BT",     "About",    "Reset"};
};

#endif