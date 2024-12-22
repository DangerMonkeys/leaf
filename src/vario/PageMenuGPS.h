#ifndef PageMenuGPS_h
#define PageMenuGPS_h

#include <Arduino.h>
#include "menu_page.h"

class GPSMenuPage : public SettingsMenuPage {
  public:
    GPSMenuPage() {
      cursor_position = 0;
      cursor_max = 3;
    }
    void draw();

    void drawConstellation(uint8_t x, uint8_t y, uint16_t size);

  protected:
    void setting_change(Button dir, ButtonState state, uint8_t count);

  private:
    static constexpr char * labels[4] = {
      "Back",
      "Update",
      "AA",
      "BB:"
    };
};


#endif