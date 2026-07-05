#ifndef PageMenuFlightTools_h
#define PageMenuFlightTools_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class FlightToolsMenuPage : public SettingsMenuPage {
 public:
  FlightToolsMenuPage() {
    cursor_position = 0;
    cursor_max = 6;
  }
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;
  void draw();

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  bool row_hidden(int8_t row) const;
  void skip_hidden_forward();
  void skip_hidden_backward();
};

#endif
