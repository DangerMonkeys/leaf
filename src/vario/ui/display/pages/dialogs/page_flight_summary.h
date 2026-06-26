#pragma once
#include <etl/array.h>
#include <etl/array_view.h>

#include "logbook/flight_stats.h"
#include "ui/display/menu_page.h"

class PageFlightSummary : public SimpleSettingsMenuPage {
 public:
  const char* get_title() const override { return "Flight Summary"; }
  virtual void draw_extra() override;
  virtual etl::array_view<const char*> get_labels() const override { return labels; }
  virtual void setting_change(Button dir, ButtonEvent state, uint8_t count) override;
  void closed(bool removed_from_Stack) override {
    if (removed_from_Stack) showing_ = false;
  }
  void show(const FlightStats stats, const String& logbookPath, const String& trackPath) {
    this->stats = stats;
    this->logbookPath = logbookPath;
    this->trackPath = trackPath;
    this->deleted = false;
    showing_ = true;
    push_page(this);
  }
  static bool isShowing() { return showing_; }

 private:
  FlightStats stats;
  String logbookPath;
  String trackPath;
  bool deleted = false;
  static etl::array<const char*, 1> labels;
  static bool showing_;
};
