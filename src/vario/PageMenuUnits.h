#ifndef PageMenuUnits_h
#define PageMenuUnits_h

#include <Arduino.h>
#include "menu_page.h"

class UnitsMenuPage : public SettingsMenuPage {
  public:
    UnitsMenuPage() {
      cursor_position = 0;
      cursor_max = 7;
    }
    void draw();

  protected:
    void setting_change(buttons dir, button_states state, uint8_t count);

  private:
    static constexpr char * labels[8] = {
      "Back",
      "Altitude",
      "ClimbRate",
      "Speed",
      "Distance",
      "Heading",
      "Temp",
      "Time"
    };
};


#endif