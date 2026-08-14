#ifndef PageMenuLeafLabs_h
#define PageMenuLeafLabs_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class LeafLabsMenuPage : public SettingsMenuPage {
 public:
  LeafLabsMenuPage() {
    cursor_position = 0;
    cursor_max = 3;
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  static constexpr char* labels[4] = {"Back", "Thermal\037Core", "Thermal\037Track", "Leaf Log"};
};

#endif
