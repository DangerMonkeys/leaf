#ifndef PageMenuNavData_h
#define PageMenuNavData_h

#include <Arduino.h>

#include "ui/display/menu_page.h"
#include "ui/display/pages/menu/page_gpx_file_select.h"
#include "ui/display/pages/menu/page_nav_data_select.h"
#include "ui/input/buttons.h"

class NavDataMenuPage : public SettingsMenuPage {
 public:
  NavDataMenuPage() {
    cursor_position = 0;
    cursor_max = 4;
  }
  void draw();
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;

 protected:
  void setting_change(Button dir, ButtonEvent state, uint8_t count);

 private:
  bool row_hidden(int8_t row) const;
  void skip_hidden_forward();
  void skip_hidden_backward();
  void drawFittedText(uint8_t x, uint8_t y, const char* text, uint8_t maxWidth);

  PageGpxFileSelect gpxFileSelectPage;
  PageNavDataSelect navDataSelectPage;
};

#endif
