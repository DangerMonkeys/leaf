#pragma once

#include "Arduino.h"
#include "logbook/flight_stats.h"
#include "logbook/logbook_io.h"
#include "logbook/logbook_store.h"
#include "profiles/profile_store.h"

class LogbookEntryFile {
 public:
  bool begin(const FlightStats& stats);
  bool writePlaceholder(const FlightStats& stats);
  void captureFirstFix(const FlightStats& stats);
  bool refreshStartTimeFromSyncedClock(const FlightStats& stats);
  bool finalize(const FlightStats& stats, const String& trackFormat = "",
                const String& trackPath = "");
  void setTrack(const String& format, const String& path);
  void reset();
  void summary(const FlightStats& stats, LogbookEntrySummary& summary) const;
  static bool deleteFiles(const String& logbookPath, const String& trackPath);

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
  void refreshFirstFixTimeIfPossible();
  bool writeJson(const FlightStats& stats, bool finalEntry);
  bool renameToSyncedTimestampIfNeeded();

  String flightId_;
  String path_;
  bool startTimeValid_ = false;
  bool placeholderWritten_ = false;
  bool firstFixCaptured_ = false;
  bool firstFixTimeValid_ = false;
  time_t startEpoch_ = 0;
  time_t firstFixEpoch_ = 0;
  unsigned long firstFixDurationSeconds_ = 0;
  float startTemperatureC_ = 0;
  float firstFixTemperatureC_ = 0;
  String trackFormat_;
  String trackPath_;
  bool placeholderPending_ = true;
  uint8_t placeholderAttempts_ = 0;
  uint32_t revision_ = 0;
  bool finalStarted_ = false;
  logbook_io::PendingWrite pendingWrite_;
  time_t endEpoch_ = 0;
  int16_t timezoneMinutes_ = 0;
  bool endTimeValid_ = false;
  PilotProfile pilot_;
  GliderProfile glider_;
};
