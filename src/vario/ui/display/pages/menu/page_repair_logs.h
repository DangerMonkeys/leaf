#pragma once
#include "ui/display/menu_page.h"

// Explicit maintenance; no per-flight state is retained in RAM.
class PageRepairLogs : public MenuPage {
 public:
  void start();
  void draw() override;
  bool button_event(Button button, ButtonEvent state, uint8_t count) override;

 protected:
  void closed(bool removed) override;

 private:
  enum class Phase : uint8_t { Checking, Repairing, Result };
  Phase phase_ = Phase::Checking;
  uint32_t visibleSince_ = 0;
  bool drawn_ = false, blocked_ = false, busy_ = false, ownsRepair_ = false;
  void setPhase(Phase phase);
};
