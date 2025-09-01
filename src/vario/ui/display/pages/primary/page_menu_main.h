#ifndef PageMenuMain_h
#define PageMenuMain_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class MainMenuPage : public MenuPage {
 public:
  MainMenuPage() {
    cursor_position = 0;
    cursor_max = 9;
  }
  bool button_event(Button button, ButtonEvent state, uint8_t count);
  void draw();
  void backToMainMenu();
  void quitMenu();

 private:
  void draw_main_menu();
  bool mainMenuButtonEvent(Button button, ButtonEvent state, uint8_t count);
  void menu_item_action(Button dir);
  static constexpr char* labels[10] = {"Back", "Altimeter", "Vario", "Display", "Units",
                                       "GPS",  "Log/Timer", "Fanet", "System",  "Developer"};
};

#endif