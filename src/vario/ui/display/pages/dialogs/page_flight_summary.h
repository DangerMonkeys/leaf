#pragma once
#include "logbook/flight_stats.h"
#include "ui/display/menu_page.h"

class PageFlightSummary : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Flight Summary"; }
  virtual void draw_extra() override;
  void show(const FlightStats stats) {
    this->stats = stats;
    push_page(this);
  }

 private:
  FlightStats stats;
};