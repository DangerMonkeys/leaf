#ifndef PageMenuDisplay_h
#define PageMenuDisplay_h

#include <Arduino.h>
#include "menu_page.h"

class DisplayMenuPage : public SettingsMenuPage {
  public:
    DisplayMenuPage() {
      cursor_position = 0;
      cursor_max = 7;
    }
    void draw();

  protected:
    void setting_change(buttons dir, button_states state, uint8_t count);

  private:
    static constexpr char * labels[8] = {
      "Back",
      "Contrast",
      "AA",
      "BB",
      "CC",
      "DD",
      "EE",
      "FF"
    };
};


#endif