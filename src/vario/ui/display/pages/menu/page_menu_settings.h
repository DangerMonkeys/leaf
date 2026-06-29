#ifndef PageMenuSettings_h
#define PageMenuSettings_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class SettingsRootMenuPage : public SettingsMenuPage {
 public:
  SettingsRootMenuPage() {
    cursor_position = 0;
    cursor_max = 8;
  }
  void draw();
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;
  void backToSettingsMenu();
  void focusSystemUpdate();

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  void drawSettingsMenu();
  bool settingsRootButtonEvent(Button button, ButtonEvent state, uint8_t count);
  bool row_hidden(uint8_t row) const;
  void skip_hidden_forward();
  void skip_hidden_backward();
};

#endif
