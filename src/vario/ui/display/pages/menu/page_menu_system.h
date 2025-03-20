#ifndef PageMenuSystem_h
#define PageMenuSystem_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/display/pages/menu/system/page_menu_system_wifi.h"
#include "ui/input/buttons.h"

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