#pragma once

#include "menu_page.h"

// Draws up to 4 neighbors
class PageFanetNeighbors : public SimpleSettingsMenuPage {
 public:
  static void show();
  const char* get_title() const override { return "Fanet Neighbors"; }
  void draw_extra() override;
};