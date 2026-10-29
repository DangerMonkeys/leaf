#include "logbook_entry.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <esp_system.h>
#include <math.h>
#include <time.h>

#include <esp_heap_caps.h>
#include "instruments/gps.h"
#include "logbook/logbook_io.h"
#include "profiles/profile_store.h"
#include "system/version_info.h"
#include "ui/settings/settings.h"

namespace {
  constexpr const char* LOGBOOK_DIR = "/logbook";
  constexpr const char* SCHEMA_NAME = "leaf.logbook.flight";
  constexpr const char* SCHEMA_VERSION = "v0.1.0";

  String absolutePath(const String& path) {
    if (path.isEmpty() || path[0] == '/') return path;
    return "/" + path;
  }

  void formatUtcTime(char* out, size_t len, time_t epoch) {
    tm cal;
    gmtime_r(&epoch, &cal);
    strftime(out, len, "%FT%TZ", &cal);
  }

  void formatLocalTime(char* out, size_t len, time_t epoch, int16_t offsetMinutes) {
    tm cal;
    const time_t localEpoch = epoch + time_t(offsetMinutes) * 60;
    gmtime_r(&localEpoch, &cal);
    strftime(out, len, "%FT%T", &cal);
  }

  void addSystemTime(JsonObject event, bool valid, time_t epoch, int16_t offsetMinutes) {
    event["time_valid"] = valid;
    if (!valid) return;

    char utc[24];
    char local[24];
    formatUtcTime(utc, sizeof(utc), epoch);
    formatLocalTime(local, sizeof(local), epoch, offsetMinutes);
    event["time_utc"] = utc;
    event["time_local"] = local;
  }

  void addFloat(JsonObject object, const char* key, float value) {
    if (isfinite(value)) {
      object[key] = value;
    } else {
      object[key] = nullptr;
    }
  }

  void addLocation(JsonObject event, float lat, float lon, float altitudeM, bool valid) {
    if (!valid) return;

    JsonObject location = event["location"].to<JsonObject>();
    addFloat(location, "lat_deg", lat);
    addFloat(location, "lon_deg", lon);
    addFloat(location, "altitude_m", altitudeM);
  }
}  // namespace

bool LogbookEntryFile::begin(const FlightStats& stats) {
  reset();
  flightId_ = generateFlightId();
  timezoneMinutes_ = settings.system_timeZone;
  startTimeValid_ = gps.systemTimeSyncedThisBoot();
  if (startTimeValid_) {
    startEpoch_ = startEpochFromStats(stats);
  }
  path_ = pathForStem(startTimeValid_ ? timestampFileStem() : undatedFileStem());
  startTemperatureC_ = stats.temperature;
  ProfileStore::activePilot(pilot_);
  ProfileStore::activeGlider(glider_);

  return true;
}

bool LogbookEntryFile::writePlaceholder(const FlightStats& stats) {
  if (!active()) {
    if (!begin(stats)) return false;
  }
  if (!placeholderPending_ || placeholderAttempts_ >= 2) return placeholderWritten_;
  placeholderPending_ = false;
  ++placeholderAttempts_;
  logbook_io::Guard transaction;
  if (!transaction || !ensureLogbookDirectory()) return false;
  // Before the first write the filename can change without moving anything on SD.
  // Later clock synchronization is coalesced into content; relocation waits for stop.
  if (placeholderAttempts_ == 1 && startTimeValid_) path_ = pathForStem(timestampFileStem());
  placeholderWritten_ = writeJson(stats, false);
  return placeholderWritten_;
}

void LogbookEntryFile::captureFirstFix(const FlightStats& stats) {
  if (firstFixCaptured_) return;
  if (stats.startLocationLat == 0 && stats.startLocationLng == 0) return;

  firstFixCaptured_ = true;
  firstFixTimeValid_ = gps.systemTimeSyncedThisBoot();
  firstFixDurationSeconds_ = stats.duration;
  if (firstFixTimeValid_) {
    firstFixEpoch_ = startEpochFromStats(stats) + stats.duration;
  }
  firstFixTemperatureC_ = stats.temperature;
  placeholderWritten_ = false;
  placeholderPending_ = true;
}

bool LogbookEntryFile::refreshStartTimeFromSyncedClock(const FlightStats& stats) {
  if (!active() || startTimeValid_ || !gps.systemTimeSyncedThisBoot()) return false;
  startTimeValid_ = true;
  startEpoch_ = startEpochFromStats(stats);
  refreshFirstFixTimeIfPossible();
  placeholderWritten_ = false;
  placeholderPending_ = true;
  return true;
}

void LogbookEntryFile::setTrack(const String& format, const String& path) {
  if (trackPath_ == path && trackFormat_ == format) return;
  trackFormat_ = format;
  trackPath_ = path;
  placeholderWritten_ = false;
  placeholderPending_ = true;
}

bool LogbookEntryFile::finalize(const FlightStats& stats, const String& trackFormat,
                                const String& trackPath) {
  if (!active() && !begin(stats)) return false;
  setTrack(trackFormat, trackPath);
  endEpoch_ = time(nullptr);
  endTimeValid_ = gps.systemTimeSyncedThisBoot();
  if (!startTimeValid_ && endTimeValid_) {
    startTimeValid_ = true;
    startEpoch_ = startEpochFromStats(stats);
    refreshFirstFixTimeIfPossible();
  }
  const uint32_t started = millis();
  const size_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  bool saved = false;
  unsigned attempts = 0;
  for (; attempts < 3 && (attempts == 0 || millis() - started < 2000);) {
    ++attempts;
    {
      logbook_io::Guard transaction;
      if (transaction && ensureLogbookDirectory()) {
        saved = writeJson(stats, true);
        // Rename only our known, completed main; no candidate scan/readback is needed.
        if (saved && startTimeValid_) renameToSyncedTimestampIfNeeded();
      }
    }
    if (saved || !logbook_io::retryable() || attempts == 3 || millis() - started >= 1900) break;
    delay(100);  // Yield to the system between attempts; no background retry state is retained.
  }
  Serial.printf(
      "logbook finalization: saved=%d attempts=%u elapsed_ms=%lu heap_before=%u heap_after=%u "
      "stats_bytes=%u entry_bytes=%u\n",
      saved, attempts, static_cast<unsigned long>(millis() - started), unsigned(heapBefore),
      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(sizeof(FlightStats)), unsigned(sizeof(LogbookEntryFile)));
  return saved;
}

void LogbookEntryFile::reset() {
  flightId_ = String();
  path_ = String();
  startTimeValid_ = false;
  placeholderWritten_ = false;
  firstFixCaptured_ = false;
  firstFixTimeValid_ = false;
  startEpoch_ = 0;
  firstFixEpoch_ = 0;
  firstFixDurationSeconds_ = 0;
  startTemperatureC_ = 0;
  firstFixTemperatureC_ = 0;
  trackFormat_ = String();
  trackPath_ = String();
  placeholderPending_ = true;
  placeholderAttempts_ = 0;
  revision_ = 0;
  finalStarted_ = false;
  pendingWrite_ = {};
  endEpoch_ = 0;
  endTimeValid_ = false;
  timezoneMinutes_ = 0;
  pilot_ = PilotProfile();
  glider_ = GliderProfile();
}

bool LogbookEntryFile::deleteFiles(const String& logbookPath, const String& trackPath) {
  return logbook_io::erase(absolutePath(logbookPath), trackPath);
}

String LogbookEntryFile::generateFlightId() const {
  char id[9];
  snprintf(id, sizeof(id), "%08lx", static_cast<unsigned long>(esp_random()));
  return String(id);
}

String LogbookEntryFile::timestampFileStem() const {
  tm cal;
  const time_t localEpoch = startEpoch_ + time_t(timezoneMinutes_) * 60;
  gmtime_r(&localEpoch, &cal);

  char stem[32];
  strftime(stem, sizeof(stem), "%F_%H-%M-%S", &cal);
  return String(stem) + "_" + flightId_;
}

String LogbookEntryFile::undatedFileStem() const {
  return String("date_not_available_") + String(millis() / 1000) + "_" + flightId_;
}

String LogbookEntryFile::pathForStem(const String& stem) const {
  return String(LOGBOOK_DIR) + "/" + stem + ".json";
}

bool LogbookEntryFile::ensureLogbookDirectory() const { return logbook_io::ensureDirectory(); }

bool LogbookEntryFile::renameToSyncedTimestampIfNeeded() {
  const String newPath = pathForStem(timestampFileStem());
  if (newPath == path_) return true;
  if (!logbook_io::relocate(path_, newPath, false)) return false;
  path_ = newPath;
  return true;
}

time_t LogbookEntryFile::startEpochFromStats(const FlightStats& stats) const {
  const unsigned long nowSinceBoot = millis() / 1000;
  const unsigned long elapsedSinceStart =
      nowSinceBoot >= stats.logStartedAt ? nowSinceBoot - stats.logStartedAt : 0;
  return time(nullptr) - elapsedSinceStart;
}

void LogbookEntryFile::refreshFirstFixTimeIfPossible() {
  if (!firstFixCaptured_ || firstFixTimeValid_ || !startTimeValid_) return;

  firstFixTimeValid_ = true;
  firstFixEpoch_ = startEpoch_ + firstFixDurationSeconds_;
  placeholderWritten_ = false;
}

bool LogbookEntryFile::writeJson(const FlightStats& stats, bool finalEntry) {
  if (path_.isEmpty()) return false;
  logbook_io::Guard transaction;
  if (!transaction) return false;
  if (!finalEntry || !finalStarted_) {
    ++revision_;
    finalStarted_ = finalEntry;
  }
  JsonDocument doc;

  doc["schema"] = SCHEMA_NAME;
  doc["schema_version"] = SCHEMA_VERSION;
  doc["flight_id"] = flightId_;
  doc["revision"] = revision_;
  doc["complete"] = finalEntry;

  JsonObject clock = doc["clock"].to<JsonObject>();
  clock["system_time_synced_this_boot"] =
      finalEntry ? endTimeValid_ : gps.systemTimeSyncedThisBoot();
  clock["time_source"] = startTimeValid_ ? "gps" : "unsynced";
  clock["timezone_offset_minutes"] = timezoneMinutes_;

  JsonObject start = doc["start"].to<JsonObject>();
  addSystemTime(start, startTimeValid_, startEpoch_, timezoneMinutes_);
  addFloat(start, "temperature_c", startTemperatureC_);

  if (firstFixCaptured_) {
    JsonObject firstFix = doc["first_fix"].to<JsonObject>();
    addSystemTime(firstFix, firstFixTimeValid_, firstFixEpoch_, timezoneMinutes_);
    addLocation(firstFix, stats.startLocationLat, stats.startLocationLng, stats.gpsalt_start, true);
    addFloat(firstFix, "temperature_c", firstFixTemperatureC_);
  }

  if (finalEntry) {
    JsonObject end = doc["end"].to<JsonObject>();
    addSystemTime(end, endTimeValid_, endEpoch_, timezoneMinutes_);
    addLocation(end, stats.endLocationLat, stats.endLocationLng, stats.gpsalt_end,
                stats.endLocationLat != 0 || stats.endLocationLng != 0);
    addFloat(end, "temperature_c", stats.temperature);
  }

  JsonObject metrics = doc["metrics"].to<JsonObject>();
  metrics["duration_seconds"] = stats.duration;
  addFloat(metrics, "max_altitude_m", stats.gpsalt_max);
  addFloat(metrics, "min_altitude_m", stats.gpsalt_min);
  addFloat(metrics, "max_altitude_above_launch_m", stats.gpsalt_above_launch_max);
  addFloat(metrics, "max_climb_rate_mps", stats.climb_max / 100.0f);
  addFloat(metrics, "max_sink_rate_mps", stats.climb_min / 100.0f);
  addFloat(metrics, "max_ground_speed_mps", stats.speed_max);
  if (stats.windValid) {
    JsonObject maxWind = metrics["max_wind"].to<JsonObject>();
    addFloat(maxWind, "speed_mps", stats.windSpeedMax);
    addFloat(maxWind, "direction_from_deg", stats.windDirectionFromAtMaxDeg);
  }
  addFloat(metrics, "average_ground_speed_mps",
           stats.duration > 0 ? stats.distanceAlongPath / stats.duration : 0);
  addFloat(metrics, "straight_line_distance_m", stats.distanceStraightLine);
  addFloat(metrics, "path_distance_m", stats.distanceAlongPath);
  addFloat(metrics, "max_accel_g", stats.accel_max);
  addFloat(metrics, "min_accel_g", stats.accel_min);
  addFloat(metrics, "max_temperature_c", stats.temperature_max);
  addFloat(metrics, "min_temperature_c", stats.temperature_min);

  JsonObject track = doc["track"].to<JsonObject>();
  track["saved"] = !trackPath_.isEmpty();
  if (!trackPath_.isEmpty()) {
    if (!trackFormat_.isEmpty()) track["format"] = trackFormat_;
    track["path"] = trackPath_;
  }

  JsonObject device = doc["device"].to<JsonObject>();
  device["firmware_version"] = LeafVersionInfo::firmwareVersion();
  device["hardware_version"] = LeafVersionInfo::hardwareVariant();
  device["mac_address"] = settings.macAddress;

  JsonObject pilot = doc["pilot"].to<JsonObject>();
  if (pilot_.valid()) {
    pilot["id"] = pilot_.id;
    pilot["name"] = pilot_.name;
  } else {
    pilot["id"] = nullptr;
    pilot["name"] = nullptr;
  }

  JsonObject glider = doc["glider"].to<JsonObject>();
  if (glider_.valid()) {
    glider["id"] = glider_.id;
    glider["brand"] = glider_.brand.isEmpty() ? nullptr : glider_.brand.c_str();
    glider["model"] = glider_.model;
    glider["size"] = glider_.size.isEmpty() ? nullptr : glider_.size.c_str();
    String displayName = glider_.resolvedDisplayName();
    glider["display_name"] = displayName.isEmpty() ? nullptr : displayName.c_str();
  } else {
    glider["id"] = nullptr;
    glider["brand"] = nullptr;
    glider["model"] = nullptr;
    glider["size"] = nullptr;
    glider["display_name"] = nullptr;
  }
  JsonObject site = doc["site"].to<JsonObject>();
  site["launch_name"] = nullptr;
  site["landing_name"] = nullptr;
  doc["weather"].to<JsonObject>();
  JsonObject notes = doc["notes"].to<JsonObject>();
  notes["pilot_notes"] = "";
  notes["tags"].to<JsonArray>();

  return logbook_io::commit(path_, doc, 0, &pendingWrite_);
}

void LogbookEntryFile::summary(const FlightStats& stats, LogbookEntrySummary& result) const {
  result = LogbookEntrySummary();
  result.valid = true;
  result.path = path_;
  result.filename = LogbookStore::filenameFromPath(path_);
  result.flightId = flightId_;
  result.pilotName = pilot_.name;
  result.gliderDisplayName = glider_.resolvedDisplayName();
  result.startTimeValid = startTimeValid_;
  if (startTimeValid_) {
    char utc[24], local[24];
    formatUtcTime(utc, sizeof(utc), startEpoch_);
    formatLocalTime(local, sizeof(local), startEpoch_, timezoneMinutes_);
    result.startTimeUtc = utc;
    result.startTimeLocal = local;
  }
  result.startAltitudeM = stats.gpsalt_start;
  result.endAltitudeM = stats.gpsalt_end;
  result.durationSeconds = stats.duration;
  result.maxAltitudeM = stats.gpsalt_max;
  result.minAltitudeM = stats.gpsalt_min;
  result.maxAltitudeAboveLaunchM = stats.gpsalt_above_launch_max;
  result.maxClimbRateMps = stats.climb_max / 100.0f;
  result.maxSinkRateMps = stats.climb_min / 100.0f;
  result.maxGroundSpeedMps = stats.speed_max;
  result.maxWindValid = stats.windValid;
  result.maxWindSpeedMps = stats.windSpeedMax;
  result.maxWindDirectionFromDeg = stats.windDirectionFromAtMaxDeg;
  result.pathDistanceM = stats.distanceAlongPath;
  result.straightLineDistanceM = stats.distanceStraightLine;
  result.maxAccelG = stats.accel_max;
  result.minAccelG = stats.accel_min;
  result.maxTemperatureC = stats.temperature_max;
  result.minTemperatureC = stats.temperature_min;
  result.trackSaved = !trackPath_.isEmpty();
  result.trackPath = trackPath_;
  result.trackFormat = trackFormat_;
}
