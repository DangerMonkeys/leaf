#ifndef page_menu_units_h
#define page_menu_units_h

#include <Arduino.h>
#include "menu_page.h"

class VarioMenuPage : public SettingsMenuPage {
  public:
    VarioMenuPage() {
      cursor_position = 0;
      cursor_max = 7;
    }
    void draw();

  protected:
    void setting_change(Button dir, ButtonState state, uint8_t count);

  private:
    static constexpr char * labels[8] = {
      "Back",
      "BeepVolume",
      "Sens",
      "Tones",
      "LiftyAir",
      "ClimbAvg",
      "ClimbStart",
      "SinkAlarm",  
    };
};


#endif