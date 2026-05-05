#pragma once

#include "ui/display/menu_page.h"

class PageAlertAutoOff : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "! AUTO OFF !"; }
  void draw_extra() override;
  void show();
  void setting_change(Button dir, ButtonEvent state, uint8_t count) override;
  void closeAlert() { pop_page(); }
};