#ifndef PageMenuDisplay_h
#define PageMenuDisplay_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class DisplayMenuPage : public SettingsMenuPage {
 public:
  DisplayMenuPage() {
    cursor_position = 0;
    cursor_max = 6;
  }
  void draw();
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);
  bool cursorUsesLeftButton() const override;

 private:
  static constexpr char* labels[7] = {
      "Back", "Basic", "User", "Thermal\037Core", "Thermal\037Track", "Navigate", "Contrast"};
  bool row_hidden(uint8_t row) const;
  uint8_t row_y(uint8_t row) const;
  void skip_hidden_forward();
  void skip_hidden_backward();
};

#endif
