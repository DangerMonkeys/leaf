#pragma once

#include "ui/display/display.h"
#include "ui/display/fonts.h"
#include "ui/display/menu_page.h"

class PageFanet : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Fanet"; }
  static void show() { push_page(&getInstance()); }
  etl::array_view<const char*> get_labels() const override {
    static etl::array labels{"Ground", "Region", "Stats", "Neighbors"};
    return etl::array_view<const char*>(labels);
  }

  // Menu item button icons will depend on the current setting
  virtual void draw_menu_input(int8_t cursor_position) override;

 protected:
  void setting_change(Button dir, ButtonState state, uint8_t count) override;

 private:
  static PageFanet& getInstance();
};
