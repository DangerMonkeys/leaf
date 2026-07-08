#include "navigation/user_waypoints.h"

#include <SD_MMC.h>
#include <math.h>
#include <string.h>

#include "diagnostics/heap_monitor.h"
#include "instruments/gps.h"
#include "storage/sd_card.h"

namespace user_waypoints {
  namespace {
    constexpr const char* WAYPOINTS_DIR = "/waypoints";
    constexpr const char* TEMP_FILE = "/waypoints/user_waypoints.tmp";
    constexpr size_t USER_WAYPOINTS_MAX_BYTES = 24576;

    bool ensureDirectory() { return SD_MMC.exists(WAYPOINTS_DIR) || SD_MMC.mkdir(WAYPOINTS_DIR); }

    String timestampText(const char* format, const String& fallback) {
      tm cal;
      if (!gps.getUtcDateTime(cal)) return fallback;
      char buffer[24];
      strftime(buffer, sizeof(buffer), format, &cal);
      return String(buffer);
    }

    String defaultPointName() {
      tm cal;
      if (!gps.getLocalDateTime(cal)) return "Saved Point";
      char buffer[24];
      strftime(buffer, sizeof(buffer), "Saved %H:%M", &cal);
      return String(buffer);
    }

    String cleanName(String name) {
      name.trim();
      if (name.isEmpty()) name = "Saved Point";
      if (name.length() > maxGpxNameLength) name = name.substring(0, maxGpxNameLength);
      return name;
    }

    bool validPoint(JsonObjectConst point) {
      const double lat = point["lat"] | NAN;
      const double lon = point["lon"] | NAN;
      return !isnan(lat) && !isnan(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
    }

    uint8_t navigatorIndexForPoint(JsonObjectConst point) {
      Waypoint waypoint;
      waypoint.setLatitude(point["lat"] | 0.0);
      waypoint.setLongitude(point["lon"] | 0.0);
      for (uint8_t i = 1; i <= navigator.totalWaypoints; i++) {
        if (navigator.waypoints[i].latE7 == waypoint.latE7 &&
            navigator.waypoints[i].lonE7 == waypoint.lonE7) {
          return i;
        }
      }
      return 0;
    }

    bool loadDocument(JsonDocument& doc, String& error) {
      doc.clear();
      if (!SD_MMC.exists(filePath())) {
        doc["schema"] = "leaf.user_waypoints";
        doc["schema_version"] = "v0.1.0";
        doc["points"].to<JsonArray>();
        return true;
      }

      File file = SD_MMC.open(filePath(), "r");
      if (!file) {
        error = "Unable to read user waypoints.";
        return false;
      }
      if (file.size() > USER_WAYPOINTS_MAX_BYTES) {
        file.close();
        error = "User waypoint file is too large.";
        return false;
      }

      DeserializationError parseError = deserializeJson(doc, file);
      file.close();
      if (parseError) {
        error = "User waypoint file is invalid.";
        return false;
      }

      const char* schema = doc["schema"] | "";
      if (strcmp(schema, "leaf.user_waypoints") != 0) {
        error = "User waypoint file has an unsupported schema.";
        return false;
      }
      if (!doc["points"].is<JsonArray>()) doc["points"].to<JsonArray>();
      return true;
    }

    bool writeDocument(JsonDocument& doc, String& error) {
      if (!ensureDirectory()) {
        error = "Unable to create waypoints folder.";
        return false;
      }

      if (SD_MMC.exists(TEMP_FILE)) SD_MMC.remove(TEMP_FILE);
      File file = SD_MMC.open(TEMP_FILE, "w", true);
      if (!file) {
        error = "Unable to write user waypoints.";
        return false;
      }

      const size_t written = serializeJson(doc, file);
      file.close();
      if (written == 0) {
        SD_MMC.remove(TEMP_FILE);
        error = "Unable to write user waypoints.";
        return false;
      }

      if (SD_MMC.exists(filePath()) && !SD_MMC.remove(filePath())) {
        SD_MMC.remove(TEMP_FILE);
        error = "Unable to replace user waypoints.";
        return false;
      }
      if (!SD_MMC.rename(TEMP_FILE, filePath())) {
        SD_MMC.remove(TEMP_FILE);
        error = "Unable to save user waypoints.";
        return false;
      }
      return true;
    }

    Waypoint waypointFromJson(JsonObjectConst point) {
      Waypoint waypoint;
      waypoint.setName(cleanName(point["name"] | "").c_str());
      waypoint.setLatitude(point["lat"] | 0.0);
      waypoint.setLongitude(point["lon"] | 0.0);
      waypoint.ele = point["alt_m"] | 0.0;
      return waypoint;
    }

    WaypointID appendNavigatorWaypoint(JsonObjectConst point) {
      if (!validPoint(point)) return WaypointID::None;
      Waypoint waypoint = waypointFromJson(point);

      for (uint8_t i = 1; i <= navigator.totalWaypoints; i++) {
        if (navigator.waypoints[i].latE7 == waypoint.latE7 &&
            navigator.waypoints[i].lonE7 == waypoint.lonE7) {
          navigator.waypoints[i] = waypoint;
          return WaypointID(i);
        }
      }

      if (!navigator.addWaypoint(waypoint)) return WaypointID::None;
      return WaypointID(navigator.totalWaypoints);
    }
  }  // namespace

  bool appendCurrentPosition(Waypoint& savedWaypoint, String& error) {
    heap_monitor::checkpoint("user-wpt-add-start");
    savedWaypoint = Waypoint();
    if (!sdcard.isMounted()) {
      error = "SD card is not mounted.";
      return false;
    }
    if (!gps.hasUsableFix()) {
      error = "GPS fix is required.";
      return false;
    }

    JsonDocument doc;
    if (!loadDocument(doc, error)) return false;

    JsonArray points = doc["points"].as<JsonArray>();
    if (points.size() >= maxNavPoints) {
      error = "User waypoint file is full.";
      return false;
    }

    const String name = defaultPointName();
    const String id = timestampText("%Y%m%d-%H%M%S", String(millis()));
    const String created = timestampText("%Y-%m-%dT%H:%M:%SZ", "");

    JsonObject point = points.add<JsonObject>();
    point["id"] = id;
    point["name"] = name;
    point["lat"] = gps.location.lat();
    point["lon"] = gps.location.lng();
    point["alt_m"] = gps.altitude.meters();
    point["created_utc"] = created;
    point["source"] = "flight_menu";

    if (!writeDocument(doc, error)) {
      heap_monitor::checkpoint("user-wpt-add-fail");
      return false;
    }

    savedWaypoint.setName(name.c_str());
    savedWaypoint.setLatitude(gps.location.lat());
    savedWaypoint.setLongitude(gps.location.lng());
    savedWaypoint.ele = gps.altitude.meters();
    navigator.addOrFindWaypoint(savedWaypoint);
    heap_monitor::checkpoint("user-wpt-add-end");
    return true;
  }

  bool loadIntoNavigator() {
    heap_monitor::checkpoint("user-wpt-load-nav");
    if (!sdcard.isMounted() || !SD_MMC.exists(filePath())) return false;
    JsonDocument doc;
    String error;
    if (!loadDocument(doc, error)) return false;
    for (JsonObjectConst point : doc["points"].as<JsonArrayConst>()) {
      appendNavigatorWaypoint(point);
    }
    return true;
  }

  bool hasSavedPoints() {
    if (!sdcard.isMounted() || !SD_MMC.exists(filePath())) return false;
    JsonDocument doc;
    String error;
    if (!loadDocument(doc, error)) return false;
    for (JsonObjectConst point : doc["points"].as<JsonArrayConst>()) {
      if (validPoint(point)) return true;
    }
    return false;
  }

  bool loadAsNavigatorSource(bool persist) {
    heap_monitor::checkpoint("user-wpt-source-start");
    if (!sdcard.isMounted() || !SD_MMC.exists(filePath())) return false;
    JsonDocument doc;
    String error;
    if (!loadDocument(doc, error)) return false;

    navigator.clear();
    for (JsonObjectConst point : doc["points"].as<JsonArrayConst>()) {
      if (!validPoint(point)) continue;
      if (!appendNavigatorWaypoint(point)) {
        navigator.clear();
        return false;
      }
    }

    if (navigator.totalWaypoints == 0) {
      navigator.clear();
      return false;
    }

    navigator.setLoadedUserWaypointsFilename(filePath());
    const bool saved = !persist || navigator.savePersistedState();
    heap_monitor::checkpoint(saved ? "user-wpt-source-end" : "user-wpt-source-fail");
    return saved;
  }

  bool renameFromJson(JsonDocument& input, String& error) {
    heap_monitor::checkpoint("user-wpt-rename-start");
    if (!sdcard.isMounted()) {
      error = "SD card is not mounted.";
      return false;
    }

    JsonArrayConst updates = input["points"].as<JsonArrayConst>();
    if (updates.isNull()) {
      error = "Waypoint update is missing points.";
      return false;
    }

    JsonDocument doc;
    if (!loadDocument(doc, error)) return false;

    JsonArray points = doc["points"].as<JsonArray>();
    for (JsonObject point : points) {
      const char* id = point["id"] | "";
      for (JsonObjectConst update : updates) {
        if (strcmp(id, update["id"] | "") != 0) continue;
        point["name"] = cleanName(update["name"] | "");
        break;
      }
    }

    const bool written = writeDocument(doc, error);
    heap_monitor::checkpoint(written ? "user-wpt-rename-end" : "user-wpt-rename-fail");
    return written;
  }

  bool deleteById(const char* id, String& error) {
    heap_monitor::checkpoint("user-wpt-delete-start");
    if (!sdcard.isMounted()) {
      error = "SD card is not mounted.";
      return false;
    }
    if (!id || strlen(id) == 0) {
      error = "Choose a saved waypoint.";
      return false;
    }

    JsonDocument doc;
    if (!loadDocument(doc, error)) return false;

    JsonArray points = doc["points"].as<JsonArray>();
    for (size_t i = 0; i < points.size(); i++) {
      JsonObject point = points[i].as<JsonObject>();
      if (strcmp(point["id"] | "", id) != 0) continue;
      points.remove(i);
      const bool written = writeDocument(doc, error);
      heap_monitor::checkpoint(written ? "user-wpt-delete-end" : "user-wpt-delete-fail");
      return written;
    }

    error = "Saved waypoint was not found.";
    heap_monitor::checkpoint("user-wpt-delete-miss");
    return false;
  }

}  // namespace user_waypoints
