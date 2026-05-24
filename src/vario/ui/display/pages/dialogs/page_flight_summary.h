#pragma once
#include "logbook/flight_stats.h"
#include "ui/display/menu_page.h"

class PageFlightSummary : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Flight Summary"; }
  virtual void draw_extra() override;
  void closed(bool removed_from_Stack) override {
    if (removed_from_Stack) showing_ = false;
  }
  void show(const FlightStats stats) {
    this->stats = stats;
    showing_ = true;
    push_page(this);
  }
  static bool isShowing() { return showing_; }

 private:
  FlightStats stats;
  static bool showing_;
};
