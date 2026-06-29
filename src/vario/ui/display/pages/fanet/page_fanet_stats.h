#pragma once

#include "ui/display/menu_page.h"

/// @brief Shows stats on the Fanet module
class PageFanetStats : public SimpleSettingsMenuPage {
 public:
  static void show();
  const char* get_title() const override { return "Fanet Stats"; }
  uint8_t get_title_glyph() const override { return menu_ui::GLYPH_FANET; }
  void draw_extra() override;

 private:
  static PageFanetStats& getInstance();
};
