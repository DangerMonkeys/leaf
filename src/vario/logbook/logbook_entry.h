#pragma once

#include "Arduino.h"
#include "logbook/flight_stats.h"

class LogbookEntryFile {
 public:
  bool begin(const FlightStats& stats);
  bool writePlaceholder(const FlightStats& stats);
  void captureFirstFix(const FlightStats& stats);
  bool refreshStartTimeFromSyncedClock(const FlightStats& stats);
  bool finalize(const FlightStats& stats, const String& trackFormat = "",
                const String& trackPath = "");
  void reset();

  bool active() const { return !path_.isEmpty(); }
  const String& flightId() const { return flightId_; }
  const String& path() const { return path_; }

 private:
  String generateFlightId() const;
  String timestampFileStem() const;
  String unsyncedFileStem() const;
  String pathForStem(const String& stem) const;
  bool ensureLogbookDirectory() const;
  time_t startEpochFromStats(const FlightStats& stats) const;
  bool writeJson(const FlightStats& stats, bool finalEntry, const String& trackFormat,
                 const String& trackPath) const;
  bool renameToSyncedTimestampIfNeeded();

  String flightId_;
  String path_;
  bool startTimeValid_ = false;
  bool placeholderWritten_ = false;
  bool firstFixCaptured_ = false;
  bool firstFixTimeValid_ = false;
  time_t startEpoch_ = 0;
  time_t firstFixEpoch_ = 0;
  float startTemperatureC_ = 0;
  float firstFixTemperatureC_ = 0;
};
