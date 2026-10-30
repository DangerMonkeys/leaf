#ifndef PageMenuDeveloper_h
#define PageMenuDeveloper_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class DeveloperMenuPage : public SettingsMenuPage {
 public:
  DeveloperMenuPage() {
    cursor_position = 0;
    cursor_max = 8;
#ifdef LEAF_SHUTDOWN_FAULT_INJECTION
    cursor_max = 9;
#endif
  }
  void draw();
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  bool row_hidden(uint8_t row) const;
  static constexpr const char* labels[] = {"Back",
                                           "Fanet ReTX",
                                           "Log FANET",
                                           "Debug Page",
                                           "Run SelfTest",
                                           "Runtime:",
                                           "StartBusLog",
                                           "Detach HW",
                                           "Log Now:"
#ifdef LEAF_SHUTDOWN_FAULT_INJECTION
                                           ,
                                           "Off fault:"
#endif
  };
};

#endif
