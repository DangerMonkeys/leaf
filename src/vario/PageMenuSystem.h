#ifndef PageMenuSystem_h
#define PageMenuSystem_h

#include <Arduino.h>
#include "menu_page.h"
#include "PageMenuSystemWifi.h"

class SystemMenuPage : public SettingsMenuPage {
  public:
    SystemMenuPage() {
      cursor_position = 0;
      cursor_max = 8;
    }
    void draw();

  protected:
    void setting_change(Button dir, ButtonState state, uint8_t count);

  private:
    static constexpr char * labels[9] = {
      "Back",
      "TimeZone",
      "Volume",
      "Auto-Off",
      "Charge",
      "EcoMode",
      "Wifi",
      "BT",
      "Reset"      
    };
};


#endif