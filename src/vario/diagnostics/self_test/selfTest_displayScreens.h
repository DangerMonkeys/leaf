#pragma once

#include "ui/display/menu_page.h"

// Running Self-Test Page
class SelfTest_PageRunning : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Self Test"; }

  bool button_event(Button button, ButtonEvent state, uint8_t count) override {
    // The self-test runner owns flow control while tests are running.
    return false;
  }
  void show();
  void draw_extra() override;
  void close();
};

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

// Vario Ready Prompt Page
class SelfTest_PageVarioReady : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Vario Test"; }

  bool button_event(Button button, ButtonEvent state, uint8_t count) override {
    // Button polling is handled by the self-test runner so this page remains passive.
    return false;
  }
  void show();
  void draw_extra() override;
  void close() { pop_page(); }
};

// Speaker Self-Test Page
class SelfTest_PageSpeaker : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Speaker Test"; }

  bool button_event(Button button, ButtonEvent state, uint8_t count) override {
    // Ignore button events on this page
    return false;
  }
  void show();
  void draw_extra() override;
  void close() { pop_page(); }
};

// GPS Fix Self-Test Page
class SelfTest_PageGPSFix : public SimpleSettingsMenuPage {
 public:
  SelfTest_PageGPSFix(uint32_t* remainingSeconds, bool* cancelled)
      : remainingSeconds_(remainingSeconds), cancelled_(cancelled) {}
  const char* get_title() const override { return "GPS Fix Test"; }

  void show();
  void draw_extra() override;
  void close() { pop_page(); }

 protected:
  void closed(bool removed_from_Stack) override;

 private:
  uint32_t* remainingSeconds_;
  bool* cancelled_;
};

// Results Self-Test Page
class SelfTest_PageResults : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Test Results"; }
  void show();
  void draw_extra() override;
};

// Commissioning Confirmation Self-Test Page
class SelfTest_PageCommissioningConfirmation : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Commissioning"; }
  void show();
  void draw_extra() override;
};
