#pragma once

#include "ui/display/menu_page.h"

class PageMenuAbout : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "ABOUT"; }
  void draw_extra() override;
  void show();
};