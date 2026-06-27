#pragma once

#include "Arduino.h"

struct LogbookEntrySummary {
  bool valid = false;
  String path;
  String filename;
  String flightId;
  bool startTimeValid = false;
  String startTimeUtc;
  String startTimeLocal;
  float startAltitudeM = 0;
  float endAltitudeM = 0;
  uint32_t durationSeconds = 0;
  float maxAltitudeM = 0;
  float minAltitudeM = 0;
  float maxAltitudeAboveLaunchM = 0;
  float maxClimbRateMps = 0;
  float maxSinkRateMps = 0;
  float maxGroundSpeedMps = 0;
  bool maxWindValid = false;
  float maxWindSpeedMps = 0;
  float maxWindDirectionFromDeg = 0;
  float pathDistanceM = 0;
  float straightLineDistanceM = 0;
  float maxAccelG = 1;
  float minAccelG = 1;
  float maxTemperatureC = 0;
  float minTemperatureC = 0;
  bool trackSaved = false;
  String trackPath;
};

class LogbookStore {
 public:
  static constexpr const char* directoryPath() { return "/logbook"; }

  static uint16_t count();
  static bool newestEntryPath(String& path);
  static bool previousEntryPath(const String& currentPath, String& path);
  static bool nextEntryPath(const String& currentPath, String& path);
  static bool entryPositionNewestFirst(const String& currentPath, uint16_t& position,
                                       uint16_t& total);
  static bool readSummary(const String& path, LogbookEntrySummary& summary);
  static bool deleteEntry(const String& path);
  static bool isLogbookJsonPath(const String& path);
  static String normalizePath(const String& path);
  static String filenameFromPath(const String& path);

 private:
  static String sortKeyForPath(const String& path);
};
