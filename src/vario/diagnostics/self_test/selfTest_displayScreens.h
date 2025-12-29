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

// Vario Self-Test Page
class SelfTest_PageVario : public SimpleSettingsMenuPage {
 public:
  SelfTest_PageVario(float* altIncrease, float* altMax, float* climb, float* climbMax,
                     float* climbMin, bool* delayForCalib)
      : altIncrease_(altIncrease),
        altMax_(altMax),
        climb_(climb),
        climbMax_(climbMax),
        climbMin_(climbMin),
        delayForCalib_(delayForCalib) {}
  const char* get_title() const override { return "Vario Test"; }

  bool button_event(Button button, ButtonEvent state, uint8_t count) override {
    // Ignore button events on this page
    return false;
  }
  void show();
  void draw_extra() override;
  void close() { pop_page(); }

 private:
  float* altIncrease_;
  float* altMax_;
  float* climb_;
  float* climbMax_;
  float* climbMin_;
  bool* delayForCalib_;
};