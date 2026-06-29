#pragma once

#include "ui/display/menu_page.h"

// Draws up to 4 neighbors
class PageFanetNeighbors : public SimpleSettingsMenuPage {
 public:
  static void show();
  const char* get_title() const override { return "Fanet Neighbors"; }
  uint8_t get_title_glyph() const override { return menu_ui::GLYPH_FANET; }
  void draw_extra() override;
};
