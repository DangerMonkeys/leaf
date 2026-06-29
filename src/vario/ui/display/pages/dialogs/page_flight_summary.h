#pragma once

#include "logbook/flight_stats.h"
#include "logbook/logbook_store.h"
#include "ui/display/menu_page.h"

class PageFlightSummary : public SettingsMenuPage {
 public:
  PageFlightSummary();
  void draw() override;
  void setting_change(Button dir, ButtonEvent state, uint8_t count) override;
  void closed(bool removed_from_Stack) override {
    if (removed_from_Stack) showing_ = false;
  }
  void show(const FlightStats, const String& logbookPath, const String& trackPath) {
    this->logbookPath = logbookPath;
    this->trackPath = trackPath;
    this->deleted = false;
    this->deletePending = 0;
    this->summary = LogbookEntrySummary();
    LogbookStore::readSummary(logbookPath, this->summary);
    cursor_position = CURSOR_BACK;
    showing_ = true;
    push_page(this);
  }
  static bool isShowing() { return showing_; }

 private:
  String logbookPath;
  String trackPath;
  LogbookEntrySummary summary;
  bool deleted = false;
  uint8_t deletePending = 0;
  static bool showing_;
};
