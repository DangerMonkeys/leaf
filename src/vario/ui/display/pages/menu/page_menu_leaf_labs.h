#ifndef PageMenuLeafLabs_h
#define PageMenuLeafLabs_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class LeafLabsMenuPage : public SettingsMenuPage {
 public:
  LeafLabsMenuPage() {
    cursor_position = 0;
#ifdef LEAF_TRAINING_STUDENT
    cursor_max = 2;
#else
    cursor_max = 1;
#endif
  }
  void draw();

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
#ifdef LEAF_TRAINING_STUDENT
  static constexpr char* labels[3] = {"Back", "Leaf Log", "Training"};
#else
  static constexpr char* labels[2] = {"Back", "Leaf Log"};
#endif
};

#endif
