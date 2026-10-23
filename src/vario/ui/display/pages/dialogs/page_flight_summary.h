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
  void show(const FlightStats stats, const String& logbookPath, const String& trackPath,
            bool saved = true) {
    this->logbookPath = logbookPath;
    this->trackPath = trackPath;
    this->deletePending = 0;
    deleteFailed = false;
    saveFailed = !saved;
    this->summary = LogbookEntrySummary();
    // A failed finalization is terminal: opening its summary must not start another promotion.
    LogbookStore::readSummary(logbookPath, this->summary, saved);
    if (!saved) {
      summary.durationSeconds = stats.duration;
      summary.maxAltitudeM = stats.gpsalt_max;
      summary.minAltitudeM = stats.gpsalt_min;
      summary.maxAltitudeAboveLaunchM = stats.gpsalt_above_launch_max;
      summary.maxClimbRateMps = stats.climb_max / 100.0f;
      summary.maxSinkRateMps = stats.climb_min / 100.0f;
      summary.maxGroundSpeedMps = stats.speed_max;
      summary.pathDistanceM = stats.distanceAlongPath;
      summary.straightLineDistanceM = stats.distanceStraightLine;
      summary.maxAccelG = stats.accel_max;
      summary.minAccelG = stats.accel_min;
    }
    cursor_position = CURSOR_BACK;
    showing_ = true;
    push_page(this);
  }
  static bool isShowing() { return showing_; }

 private:
  String logbookPath;
  String trackPath;
  LogbookEntrySummary summary;
  uint8_t deletePending = 0;
  bool deleteFailed = false;
  bool saveFailed = false;
  static bool showing_;
};
