#ifndef PageMenuMain_h
#define PageMenuMain_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/input/buttons.h"

class MainMenuPage : public MenuPage {
 public:
  MainMenuPage() {
    cursor_position = 0;
    cursor_max = 7;
  }
  bool button_event(Button button, ButtonEvent state, uint8_t count);
  void draw();
  void backToMainMenu();
  void quitMenu();

 private:
  void draw_main_menu();
  bool mainMenuButtonEvent(Button button, ButtonEvent state, uint8_t count);
  void menu_item_action(Button dir);
  bool row_hidden(uint8_t row) const;
  void skip_hidden_forward();
  void skip_hidden_backward();
  bool firstOpened = true;
  static constexpr char* labels[8] = {"Back", "Settings", "Flight",  "Nav Data",
                                      "GPS",  "Web App",  "Logbook", "Developer"};
  static constexpr uint8_t glyphs[8] = {0,
                                        menu_ui::GLYPH_SETTINGS,
                                        menu_ui::GLYPH_FLIGHT,
                                        menu_ui::GLYPH_NAV_DATA,
                                        menu_ui::GLYPH_GPS,
                                        menu_ui::GLYPH_WEB_APP,
                                        menu_ui::GLYPH_LOGGING,
                                        menu_ui::GLYPH_DEVELOPER};
};

#endif
