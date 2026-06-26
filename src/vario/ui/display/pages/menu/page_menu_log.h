#ifndef PageMenuLog_h
#define PageMenuLog_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/display/pages/menu/page_logbook.h"
#include "ui/input/buttons.h"

class LogMenuPage : public SettingsMenuPage {
 public:
  LogMenuPage() {
    cursor_position = 0;
    cursor_max = 5;
  }
  void draw();
  void backToLogMenu();
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  void drawLogMenu();
  static constexpr char* labels[6] = {"Back", "Format", "SaveLog", "AutoStart", "AutoStop",
                                      "Logbook"};
  PageLogbook pageLogbook;
};

#endif
