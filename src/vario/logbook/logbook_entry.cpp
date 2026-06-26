#include "logbook_entry.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <esp_system.h>
#include <time.h>

#include "instruments/gps.h"
#include "system/version_info.h"
#include "ui/settings/settings.h"

namespace {
constexpr const char* LOGBOOK_DIR = "/logbook";
constexpr const char* SCHEMA_NAME = "leaf.logbook.flight";
constexpr const char* SCHEMA_VERSION = "v0.1.0";

void formatUtcTime(char* out, size_t len, time_t epoch) {
  tm cal;
  gmtime_r(&epoch, &cal);
  strftime(out, len, "%FT%TZ", &cal);
}

void formatLocalTime(char* out, size_t len, time_t epoch) {
  tm cal;
  localtime_r(&epoch, &cal);
  strftime(out, len, "%FT%T", &cal);
}

void addSystemTime(JsonObject event, bool valid, time_t epoch) {
  event["time_valid"] = valid;
  if (!valid) return;

  char utc[24];
  char local[24];
  formatUtcTime(utc, sizeof(utc), epoch);
  formatLocalTime(local, sizeof(local), epoch);
  event["time_utc"] = utc;
  event["time_local"] = local;
}

void addLocation(JsonObject event, float lat, float lon, float altitudeM, bool valid) {
  if (!valid) return;

  JsonObject location = event["location"].to<JsonObject>();
  location["lat_deg"] = lat;
  location["lon_deg"] = lon;
  location["altitude_m"] = altitudeM;
}
}  // namespace

bool LogbookEntryFile::begin(const FlightStats& stats) {
  flightId_ = generateFlightId();
  startTimeValid_ = gps.systemTimeSyncedThisBoot();
  if (startTimeValid_) {
    startEpoch_ = startEpochFromStats(stats);
  }
  path_ = pathForStem(startTimeValid_ ? timestampFileStem() : unsyncedFileStem());
  startTemperatureC_ = stats.temperature;

  return true;
}

bool LogbookEntryFile::writePlaceholder(const FlightStats& stats) {
  if (!active()) {
    if (!begin(stats)) return false;
  }
  if (placeholderWritten_) return true;
  if (!ensureLogbookDirectory()) return false;
  placeholderWritten_ = writeJson(stats, false, "", "");
  return placeholderWritten_;
}

void LogbookEntryFile::captureFirstFix(const FlightStats& stats) {
  if (firstFixCaptured_) return;
  if (stats.startLocationLat == 0 && stats.startLocationLng == 0) return;

  firstFixCaptured_ = true;
  firstFixTimeValid_ = gps.systemTimeSyncedThisBoot();
  if (firstFixTimeValid_) {
    firstFixEpoch_ = startEpochFromStats(stats) + stats.duration;
  }
  firstFixTemperatureC_ = stats.temperature;
  placeholderWritten_ = false;
}

bool LogbookEntryFile::refreshStartTimeFromSyncedClock(const FlightStats& stats) {
  if (!active() || startTimeValid_ || !gps.systemTimeSyncedThisBoot()) return false;

  startTimeValid_ = true;
  startEpoch_ = startEpochFromStats(stats);
  if (!renameToSyncedTimestampIfNeeded()) return false;
  placeholderWritten_ = writeJson(stats, false, "", "");
  return placeholderWritten_;
}

bool LogbookEntryFile::finalize(const FlightStats& stats, const String& trackFormat,
                                const String& trackPath) {
  if (!active()) {
    if (!begin(stats)) return false;
  }
  if (!ensureLogbookDirectory()) return false;

  if (!startTimeValid_ && gps.systemTimeSyncedThisBoot()) {
    startTimeValid_ = true;
    startEpoch_ = startEpochFromStats(stats);
    if (!renameToSyncedTimestampIfNeeded()) return false;
  }

  return writeJson(stats, true, trackFormat, trackPath);
}

void LogbookEntryFile::reset() {
  flightId_ = "";
  path_ = "";
  startTimeValid_ = false;
  placeholderWritten_ = false;
  firstFixCaptured_ = false;
  firstFixTimeValid_ = false;
  startEpoch_ = 0;
  firstFixEpoch_ = 0;
  startTemperatureC_ = 0;
  firstFixTemperatureC_ = 0;
}

String LogbookEntryFile::generateFlightId() const {
  char id[9];
  snprintf(id, sizeof(id), "%08lx", static_cast<unsigned long>(esp_random()));
  return String(id);
}

String LogbookEntryFile::timestampFileStem() const {
  tm cal;
  localtime_r(&startEpoch_, &cal);

  char stem[32];
  strftime(stem, sizeof(stem), "%F_%H-%M-%S", &cal);
  return String(stem) + "_" + flightId_;
}

String LogbookEntryFile::unsyncedFileStem() const {
  return String("unsynced_") + String(millis() / 1000) + "_" + flightId_;
}

String LogbookEntryFile::pathForStem(const String& stem) const {
  return String(LOGBOOK_DIR) + "/" + stem + ".json";
}

bool LogbookEntryFile::ensureLogbookDirectory() const {
  if (SD_MMC.exists(LOGBOOK_DIR)) return true;
  return SD_MMC.mkdir(LOGBOOK_DIR);
}

bool LogbookEntryFile::renameToSyncedTimestampIfNeeded() {
  const String newPath = pathForStem(timestampFileStem());
  if (newPath == path_) return true;
  if (!SD_MMC.rename(path_, newPath)) return false;
  path_ = newPath;
  return true;
}

time_t LogbookEntryFile::startEpochFromStats(const FlightStats& stats) const {
  const unsigned long nowSinceBoot = millis() / 1000;
  const unsigned long elapsedSinceStart =
      nowSinceBoot >= stats.logStartedAt ? nowSinceBoot - stats.logStartedAt : 0;
  return time(nullptr) - elapsedSinceStart;
}

bool LogbookEntryFile::writeJson(const FlightStats& stats, bool finalEntry,
                                 const String& trackFormat, const String& trackPath) const {
  if (path_.isEmpty()) return false;

  const String tmpPath = path_ + ".tmp";
  JsonDocument doc;

  doc["schema"] = SCHEMA_NAME;
  doc["schema_version"] = SCHEMA_VERSION;
  doc["flight_id"] = flightId_;

  JsonObject clock = doc["clock"].to<JsonObject>();
  clock["system_time_synced_this_boot"] = gps.systemTimeSyncedThisBoot();
  clock["time_source"] = startTimeValid_ ? "gps" : "unsynced";
  clock["timezone_offset_minutes"] = settings.system_timeZone;

  JsonObject start = doc["start"].to<JsonObject>();
  addSystemTime(start, startTimeValid_, startEpoch_);
  start["temperature_c"] = startTemperatureC_;

  if (firstFixCaptured_) {
    JsonObject firstFix = doc["first_fix"].to<JsonObject>();
    addSystemTime(firstFix, firstFixTimeValid_, firstFixEpoch_);
    addLocation(firstFix, stats.startLocationLat, stats.startLocationLng, stats.gpsalt_start, true);
    firstFix["temperature_c"] = firstFixTemperatureC_;
  }

  if (finalEntry) {
    JsonObject end = doc["end"].to<JsonObject>();
    addSystemTime(end, gps.systemTimeSyncedThisBoot(), time(nullptr));
    addLocation(end, stats.endLocationLat, stats.endLocationLng, stats.gpsalt_end,
                stats.endLocationLat != 0 || stats.endLocationLng != 0);
    end["temperature_c"] = stats.temperature;
  }

  JsonObject metrics = doc["metrics"].to<JsonObject>();
  metrics["duration_seconds"] = stats.duration;
  metrics["max_altitude_m"] = stats.gpsalt_max;
  metrics["min_altitude_m"] = stats.gpsalt_min;
  metrics["max_altitude_above_launch_m"] = stats.gpsalt_above_launch_max;
  metrics["max_climb_rate_mps"] = stats.climb_max / 100.0f;
  metrics["max_sink_rate_mps"] = stats.climb_min / 100.0f;
  metrics["max_ground_speed_mps"] = stats.speed_max;
  metrics["average_ground_speed_mps"] =
      stats.duration > 0 ? stats.distanceAlongPath / stats.duration : 0;
  metrics["straight_line_distance_m"] = stats.distanceStraightLine;
  metrics["path_distance_m"] = stats.distanceAlongPath;
  metrics["max_accel_g"] = stats.accel_max;
  metrics["min_accel_g"] = stats.accel_min;
  metrics["max_temperature_c"] = stats.temperature_max;
  metrics["min_temperature_c"] = stats.temperature_min;

  JsonObject track = doc["track"].to<JsonObject>();
  track["saved"] = !trackPath.isEmpty();
  if (!trackPath.isEmpty()) {
    if (!trackFormat.isEmpty()) track["format"] = trackFormat;
    track["path"] = trackPath;
  }

  JsonObject device = doc["device"].to<JsonObject>();
  device["firmware_version"] = LeafVersionInfo::firmwareVersion();
  device["hardware_version"] = LeafVersionInfo::hardwareVariant();
  device["mac_address"] = settings.macAddress;

  doc["pilot"].to<JsonObject>()["name"] = nullptr;
  JsonObject glider = doc["glider"].to<JsonObject>();
  glider["brand"] = nullptr;
  glider["model"] = nullptr;
  glider["size"] = nullptr;
  JsonObject site = doc["site"].to<JsonObject>();
  site["launch_name"] = nullptr;
  site["landing_name"] = nullptr;
  doc["weather"].to<JsonObject>();
  JsonObject notes = doc["notes"].to<JsonObject>();
  notes["pilot_notes"] = "";
  notes["tags"].to<JsonArray>();

  File file = SD_MMC.open(tmpPath, "w", true);
  if (!file) return false;

  serializeJsonPretty(doc, file);
  file.println();
  file.close();

  SD_MMC.remove(path_);
  return SD_MMC.rename(tmpPath, path_);
}
