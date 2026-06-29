#ifndef PageMenuSystem_h
#define PageMenuSystem_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
// #include "ui/display/pages/menu/system/page_menu_system_wifi.h"
#include "ui/display/pages/menu/page_menu_wifi.h"
#include "ui/input/buttons.h"

class SystemMenuPage : public SettingsMenuPage {
 public:
  SystemMenuPage() {
    cursor_position = 0;
    cursor_max = 7;
  }
  void draw();
  void backToSystemMenu();
  void focusUpdate();
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  void drawSystemMenu();
  static constexpr char* labels[8] = {"Back",      "Time Zone", "Volume",   "Auto-Off",
                                      "ShowSafety", "About",     "Update FW", "Reset"};
};

#endif
