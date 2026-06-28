#include "logbook_store.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>

#include "logbook/logbook_entry.h"

namespace {
  constexpr const char* LOGBOOK_DIR = "/logbook";

  bool getNextLogbookPath(File& dir, String& path) {
    while (true) {
      String next = dir.getNextFileName();
      if (next.isEmpty()) return false;

      path = LogbookStore::normalizePath(next);
      if (LogbookStore::isLogbookJsonPath(path)) return true;
    }
  }
}  // namespace

uint16_t LogbookStore::count() {
  File dir = SD_MMC.open(LOGBOOK_DIR);
  if (!dir) return 0;

  uint16_t entries = 0;
  String path;
  while (getNextLogbookPath(dir, path)) {
    entries++;
  }
  return entries;
}

bool LogbookStore::newestEntryPath(String& path) {
  File dir = SD_MMC.open(LOGBOOK_DIR);
  if (!dir) return false;

  bool found = false;
  String bestPath;
  String bestKey;
  String candidatePath;
  while (getNextLogbookPath(dir, candidatePath)) {
    String candidateKey = sortKeyForPath(candidatePath);
    if (!found || candidateKey > bestKey) {
      found = true;
      bestPath = candidatePath;
      bestKey = candidateKey;
    }
  }

  if (!found) return false;
  path = bestPath;
  return true;
}

bool LogbookStore::previousEntryPath(const String& currentPath, String& path) {
  File dir = SD_MMC.open(LOGBOOK_DIR);
  if (!dir) return false;

  const String currentKey = sortKeyForPath(currentPath);
  bool found = false;
  String bestPath;
  String bestKey;
  String candidatePath;
  while (getNextLogbookPath(dir, candidatePath)) {
    String candidateKey = sortKeyForPath(candidatePath);
    if (candidateKey >= currentKey) continue;
    if (!found || candidateKey > bestKey) {
      found = true;
      bestPath = candidatePath;
      bestKey = candidateKey;
    }
  }

  if (!found) return false;
  path = bestPath;
  return true;
}

bool LogbookStore::nextEntryPath(const String& currentPath, String& path) {
  File dir = SD_MMC.open(LOGBOOK_DIR);
  if (!dir) return false;

  const String currentKey = sortKeyForPath(currentPath);
  bool found = false;
  String bestPath;
  String bestKey;
  String candidatePath;
  while (getNextLogbookPath(dir, candidatePath)) {
    String candidateKey = sortKeyForPath(candidatePath);
    if (candidateKey <= currentKey) continue;
    if (!found || candidateKey < bestKey) {
      found = true;
      bestPath = candidatePath;
      bestKey = candidateKey;
    }
  }

  if (!found) return false;
  path = bestPath;
  return true;
}

bool LogbookStore::entryPositionNewestFirst(const String& currentPath, uint16_t& position,
                                            uint16_t& total) {
  position = 0;
  total = 0;

  File dir = SD_MMC.open(LOGBOOK_DIR);
  if (!dir) return false;

  const String currentKey = sortKeyForPath(currentPath);
  bool found = false;
  uint16_t newerEntries = 0;
  String candidatePath;
  while (getNextLogbookPath(dir, candidatePath)) {
    const String candidateKey = sortKeyForPath(candidatePath);
    total++;
    if (candidateKey == currentKey) {
      found = true;
    } else if (candidateKey > currentKey) {
      newerEntries++;
    }
  }

  if (!found) return false;
  position = newerEntries + 1;
  return true;
}

bool LogbookStore::readSummary(const String& path, LogbookEntrySummary& summary) {
  summary = LogbookEntrySummary();
  const String normalizedPath = normalizePath(path);

  File file = SD_MMC.open(normalizedPath, "r");
  if (!file) return false;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) return false;

  summary.valid = true;
  summary.path = normalizedPath;
  summary.filename = filenameFromPath(normalizedPath);
  summary.flightId = doc["flight_id"] | "";
  JsonObject pilot = doc["pilot"];
  summary.pilotName = pilot["name"] | "";
  JsonObject glider = doc["glider"];
  summary.gliderDisplayName = glider["display_name"] | "";

  JsonObject start = doc["start"];
  summary.startTimeValid = start["time_valid"] | false;
  summary.startTimeUtc = start["time_utc"] | "";
  summary.startTimeLocal = start["time_local"] | "";
  JsonObject startLocation = start["location"];
  summary.startAltitudeM = startLocation["altitude_m"] | 0.0f;

  JsonObject end = doc["end"];
  JsonObject endLocation = end["location"];
  summary.endAltitudeM = endLocation["altitude_m"] | 0.0f;

  JsonObject metrics = doc["metrics"];
  summary.durationSeconds = metrics["duration_seconds"] | 0;
  summary.maxAltitudeM = metrics["max_altitude_m"] | 0.0f;
  summary.minAltitudeM = metrics["min_altitude_m"] | 0.0f;
  summary.maxAltitudeAboveLaunchM = metrics["max_altitude_above_launch_m"] | 0.0f;
  summary.maxClimbRateMps = metrics["max_climb_rate_mps"] | 0.0f;
  summary.maxSinkRateMps = metrics["max_sink_rate_mps"] | 0.0f;
  summary.maxGroundSpeedMps = metrics["max_ground_speed_mps"] | 0.0f;
  JsonObject maxWind = metrics["max_wind"];
  summary.maxWindValid = !maxWind.isNull();
  summary.maxWindSpeedMps = maxWind["speed_mps"] | 0.0f;
  summary.maxWindDirectionFromDeg = maxWind["direction_from_deg"] | 0.0f;
  summary.pathDistanceM = metrics["path_distance_m"] | 0.0f;
  summary.straightLineDistanceM = metrics["straight_line_distance_m"] | 0.0f;
  summary.maxAccelG = metrics["max_accel_g"] | 1.0f;
  summary.minAccelG = metrics["min_accel_g"] | 1.0f;
  summary.maxTemperatureC = metrics["max_temperature_c"] | 0.0f;
  summary.minTemperatureC = metrics["min_temperature_c"] | 0.0f;

  JsonObject track = doc["track"];
  summary.trackSaved = track["saved"] | false;
  summary.trackPath = track["path"] | "";

  return true;
}

bool LogbookStore::deleteEntry(const String& path) {
  LogbookEntrySummary summary;
  if (readSummary(path, summary)) {
    return LogbookEntryFile::deleteFiles(summary.path, summary.trackPath);
  }

  const String normalizedPath = normalizePath(path);
  if (normalizedPath.isEmpty() || !SD_MMC.exists(normalizedPath)) return false;
  return SD_MMC.remove(normalizedPath);
}

bool LogbookStore::isLogbookJsonPath(const String& path) {
  const String normalizedPath = normalizePath(path);
  if (!normalizedPath.startsWith(String(LOGBOOK_DIR) + "/")) return false;
  if (!normalizedPath.endsWith(".json")) return false;
  if (normalizedPath.endsWith(".tmp")) return false;
  return filenameFromPath(normalizedPath).length() > 5;
}

String LogbookStore::normalizePath(const String& path) {
  if (path.isEmpty()) return "";
  if (path[0] == '/') return path;
  if (path.startsWith("logbook/")) return "/" + path;
  return String(LOGBOOK_DIR) + "/" + path;
}

String LogbookStore::filenameFromPath(const String& path) {
  const int slash = path.lastIndexOf('/');
  if (slash < 0) return path;
  return path.substring(slash + 1);
}

String LogbookStore::sortKeyForPath(const String& path) {
  const String filename = filenameFromPath(normalizePath(path));
  if (filename.startsWith("unsynced_")) {
    return String("0000_") + filename;
  }
  return filename;
}
