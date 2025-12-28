#pragma once

#include "ui/display/menu_page.h"

// Buttons Self-Test Page
class SelfTest_PageButtons : public SimpleSettingsMenuPage {
 public:
  SelfTest_PageButtons(bool* up, bool* down, bool* left, bool* right, bool* center)
      : up_(up), down_(down), left_(left), right_(right), center_(center) {}
  const char* get_title() const override { return "Button Test"; }

  bool button_event(Button button, ButtonEvent state, uint8_t count) override {
    // Ignore button events on this page
    return false;
  }
  void show();
  void draw_extra() override;
  void close() { pop_page(); }

 private:
  bool* up_;
  bool* down_;
  bool* left_;
  bool* right_;
  bool* center_;
};