#include "logbook_store.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>

#include "logbook/logbook_entry.h"
#include "logbook/logbook_io.h"
#include "storage/sd_card.h"

namespace {
  constexpr const char* LOGBOOK_DIR = "/logbook";
  constexpr size_t LEAF_LOG_MAX_IGC_BYTES = 5 * 1024 * 1024;

  bool getNextLogbookPath(File& dir, String& path) {
    while (true) {
      String next = dir.getNextFileName();
      if (next.isEmpty()) return false;

      if (logbook_io::candidatePath(next, path)) return true;
    }
  }

  bool isIgcTrack(JsonObjectConst track, const String& path) {
    String format = track["format"] | "";
    format.toLowerCase();
    String lowerPath = path;
    lowerPath.toLowerCase();
    return format == "igc" || lowerPath.endsWith(".igc");
  }

  bool normalizeTrackPath(const String& raw, String& normalized) {
    normalized = raw;
    normalized.trim();
    normalized.replace('\\', '/');
    if (normalized.startsWith("tracks/")) normalized = "/" + normalized;
    if (!normalized.startsWith("/tracks/") || normalized.indexOf("/../") >= 0 ||
        normalized.endsWith("/..") || normalized.indexOf("/./") >= 0) {
      normalized = "";
      return false;
    }
    return true;
  }

  bool writeLeafLogResult(const String& path, const char* key, const String& value) {
    if (value.isEmpty()) return false;
    const String normalizedPath = LogbookStore::normalizePath(path);
    logbook_io::Guard guard;
    if (!guard) return false;
    uint32_t revision;
    uint64_t previous;
    if (!logbook_io::prepare(normalizedPath, revision, previous)) return false;
    JsonDocument doc;
    if (!logbook_io::load(normalizedPath, doc)) return false;
    doc["revision"] = revision;
    if (doc["complete"].isNull()) doc["complete"] = doc["end"].is<JsonObject>();
    JsonObject leafLog = doc["leaf_log"].to<JsonObject>();
    leafLog.clear();
    leafLog[key] = value;

    return logbook_io::commit(normalizedPath, doc, previous);
  }
}  // namespace

uint16_t LogbookStore::count() {
  logbook_io::Guard guard;
  if (!guard || !sdcard.firmwareCanAccessFilesystem()) return 0;
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
  logbook_io::Guard guard;
  if (!guard || !sdcard.firmwareCanAccessFilesystem()) return false;
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
  LogbookNavigation navigation;
  if (!navigationForPath(currentPath, navigation) || navigation.previousPath.isEmpty())
    return false;
  path = navigation.previousPath;
  return true;
}

bool LogbookStore::nextEntryPath(const String& currentPath, String& path) {
  LogbookNavigation navigation;
  if (!navigationForPath(currentPath, navigation) || navigation.nextPath.isEmpty()) return false;
  path = navigation.nextPath;
  return true;
}

bool LogbookStore::entryPositionNewestFirst(const String& currentPath, uint16_t& position,
                                            uint16_t& total) {
  LogbookNavigation navigation;
  if (!navigationForPath(currentPath, navigation)) return false;
  position = navigation.position;
  total = navigation.total;
  return true;
}

bool LogbookStore::navigationForPath(const String& currentPath, LogbookNavigation& navigation) {
  logbook_io::Guard guard;
  if (!guard || !sdcard.firmwareCanAccessFilesystem()) return false;
  navigation = LogbookNavigation();
  File dir = SD_MMC.open(LOGBOOK_DIR);
  if (!dir) return false;

  const String currentKey = sortKeyForPath(currentPath);
  uint16_t newerEntries = 0;
  String previousKey;
  String nextKey;
  String candidatePath;
  while (getNextLogbookPath(dir, candidatePath)) {
    const String candidateKey = sortKeyForPath(candidatePath);
    navigation.total++;
    if (candidateKey == currentKey) {
      navigation.found = true;
    } else if (candidateKey > currentKey) {
      newerEntries++;
      if (nextKey.isEmpty() || candidateKey < nextKey) {
        nextKey = candidateKey;
        navigation.nextPath = candidatePath;
      }
    } else if (candidateKey < currentKey) {
      if (previousKey.isEmpty() || candidateKey > previousKey) {
        previousKey = candidateKey;
        navigation.previousPath = candidatePath;
      }
    }
  }

  if (!navigation.found) return false;
  navigation.position = newerEntries + 1;
  return true;
}

bool LogbookStore::readSummary(const String& path, LogbookEntrySummary& summary, bool recover) {
  logbook_io::Guard guard;
  if (!guard || !sdcard.firmwareCanAccessFilesystem()) return false;
  summary = LogbookEntrySummary();
  const String normalizedPath = normalizePath(path);
  // Preserve a readable main on an unresolved conflict; writes/uploads remain blocked.
  if (recover) logbook_io::recover(normalizedPath);
  JsonDocument doc;
  if (!logbook_io::load(normalizedPath, doc)) return false;

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
  if (!startLocation.isNull()) {
    summary.startAltitudeM = startLocation["altitude_m"] | 0.0f;
  } else {
    JsonObject firstFix = doc["first_fix"];
    JsonObject firstFixLocation = firstFix["location"];
    summary.startAltitudeM = firstFixLocation["altitude_m"] | 0.0f;
  }

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
  summary.trackFormat = track["format"] | "";
  summary.trackPath = track["path"] | "";

  JsonObject leafLog = doc["leaf_log"];
  summary.leafLogFlightId = leafLog["flight_id"] | "";
  summary.leafLogRejection = leafLog["rejected"] | "";
  if (!summary.leafLogFlightId.isEmpty()) {
    summary.leafLogStatus = LeafLogFlightStatus::Uploaded;
  } else if (!summary.leafLogRejection.isEmpty()) {
    summary.leafLogStatus = LeafLogFlightStatus::Rejected;
  } else if (summary.trackSaved && isIgcTrack(track, summary.trackPath)) {
    summary.leafLogStatus = LeafLogFlightStatus::NotUploaded;
  }

  return true;
}

bool LogbookStore::classifyForLeafLog(const String& path, LeafLogCandidate& candidate) {
  logbook_io::Guard guard;
  if (!guard || !sdcard.firmwareCanAccessFilesystem()) return false;
  candidate = LeafLogCandidate();
  candidate.logbookPath = normalizePath(path);
  if (!logbook_io::recover(candidate.logbookPath)) return false;
  JsonDocument doc;
  if (!logbook_io::load(candidate.logbookPath, doc)) return false;

  JsonObjectConst root = doc.as<JsonObjectConst>();
  JsonObjectConst leafLog = root["leaf_log"];
  const String flightId = leafLog["flight_id"] | "";
  candidate.rejectionReason = leafLog["rejected"] | "";
  if (!flightId.isEmpty()) {
    candidate.disposition = LeafLogCandidate::Disposition::Delivered;
    return true;
  }
  if (!candidate.rejectionReason.isEmpty()) {
    candidate.disposition = LeafLogCandidate::Disposition::Rejected;
    candidate.rejectionPersisted = true;
    return true;
  }

  if (root["complete"].is<bool>() && !root["complete"].as<bool>()) return true;
  const String schema = root["schema"] | "";
  JsonObjectConst track = root["track"];
  if (schema != "leaf.logbook.flight" || track.isNull()) {
    candidate.disposition = LeafLogCandidate::Disposition::Rejected;
    candidate.rejectionReason = "invalid_logbook";
    return true;
  }

  const bool saved = track["saved"] | false;
  const String rawTrackPath = track["path"] | "";
  if (!saved || !isIgcTrack(track, rawTrackPath)) return true;
  if (rawTrackPath.isEmpty()) {
    candidate.disposition = LeafLogCandidate::Disposition::Rejected;
    candidate.rejectionReason = "invalid_logbook";
    return true;
  }
  if (!normalizeTrackPath(rawTrackPath, candidate.trackPath)) {
    candidate.disposition = LeafLogCandidate::Disposition::Rejected;
    candidate.rejectionReason = "unsafe_track_path";
    return true;
  }
  candidate.trackFilename = filenameFromPath(candidate.trackPath);
  File trackFile = SD_MMC.open(candidate.trackPath, "r");
  if (!trackFile) {
    candidate.disposition = LeafLogCandidate::Disposition::Rejected;
    candidate.rejectionReason = "missing_track";
    return true;
  }
  candidate.trackSize = trackFile.size();
  trackFile.close();
  if (candidate.trackSize == 0) {
    candidate.disposition = LeafLogCandidate::Disposition::Rejected;
    candidate.rejectionReason = "empty_track";
  } else if (candidate.trackSize > LEAF_LOG_MAX_IGC_BYTES) {
    candidate.disposition = LeafLogCandidate::Disposition::Rejected;
    candidate.rejectionReason = "too_large";
  } else {
    candidate.disposition = LeafLogCandidate::Disposition::Pending;
  }
  return true;
}

bool LogbookStore::recordLeafLogFlightId(const String& path, const String& flightId) {
  return writeLeafLogResult(path, "flight_id", flightId);
}

bool LogbookStore::recordLeafLogRejection(const String& path, const String& reason) {
  return writeLeafLogResult(path, "rejected", reason);
}

const char* LogbookStore::leafLogStatusName(LeafLogFlightStatus status) {
  switch (status) {
    case LeafLogFlightStatus::NotUploaded:
      return "not_uploaded";
    case LeafLogFlightStatus::Uploaded:
      return "uploaded";
    case LeafLogFlightStatus::Rejected:
      return "rejected";
    case LeafLogFlightStatus::NotApplicable:
    default:
      return "not_applicable";
  }
}

const char* LogbookStore::leafLogRejectionLabel(const String& reason) {
  if (reason == "invalid_logbook") return "Invalid logbook";
  if (reason == "unsafe_track_path") return "Unsafe track path";
  if (reason == "missing_track") return "Track file missing";
  if (reason == "empty_track") return "Track file empty";
  if (reason == "too_large") return "Too large";
  if (reason == "invalid_igc") return "Invalid IGC";
  return "Upload rejected";
}

bool LogbookStore::deleteEntry(const String& path) {
  // The shared eraser discovers tracks and filename aliases without running recovery.
  return logbook_io::erase(normalizePath(path));
}

bool LogbookStore::isLogbookJsonPath(const String& path) {
  const String normalizedPath = normalizePath(path);
  if (!normalizedPath.startsWith(String(LOGBOOK_DIR) + "/")) return false;
  if (!normalizedPath.endsWith(".json")) return false;
  if (normalizedPath.indexOf("..") >= 0 || normalizedPath.indexOf('\\') >= 0 ||
      normalizedPath.indexOf('/', 9) >= 0)
    return false;
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
