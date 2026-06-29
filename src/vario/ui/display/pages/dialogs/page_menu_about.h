#pragma once

#include "ui/display/menu_page.h"

class PageMenuAbout : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "ABOUT"; }
  uint8_t get_title_glyph() const override { return menu_ui::GLYPH_SETTINGS; }
  void draw_extra() override;
  void show();
};
