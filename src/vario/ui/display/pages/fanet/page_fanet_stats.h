#pragma once

#include "ui/display/menu_page.h"

/// @brief Shows stats on the Fanet module
class PageFanetStats : public SimpleSettingsMenuPage {
 public:
  static void show();
  const char* get_title() const override { return "Fanet Stats"; }
  void draw_extra() override;

 private:
  static PageFanetStats& getInstance();
};