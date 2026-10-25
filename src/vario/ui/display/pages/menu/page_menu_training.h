#pragma once
#ifdef LEAF_TRAINING_STUDENT
#include "ui/display/menu_page.h"
class TrainingMenuPage : public SettingsMenuPage {
 public:
  static void show();
  void draw() override;

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count) override;

 private:
  TrainingMenuPage() {
    cursor_position = 1;
    cursor_max = 3;
  }
  static TrainingMenuPage& instance();
};
#endif
