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

    void skipWhitespace(File& file) {
      while (true) {
        const int c = file.peek();
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return;
        file.read();
      }
    }

    bool consume(File& file, int expected) {
      skipWhitespace(file);
      if (file.peek() != expected) return false;
      file.read();
      return true;
    }

    // The JSON parser reads one character ahead for numbers. Leave a parent
    // delimiter in the file so the caller can validate it.
    struct ScalarInput {
      File& file;
      int read() {
        const int c = file.peek();
        return c == ',' || c == ']' || c == '}' ? -1 : file.read();
      }
      size_t readBytes(char* out, size_t count) {
        size_t i = 0;
        for (; i < count; ++i) {
          const int c = read();
          if (c < 0) break;
          out[i] = static_cast<char>(c);
        }
        return i;
      }
    };

    bool readValue(File& file, JsonDocument& doc, uint8_t nesting) {
      skipWhitespace(file);
      const int c = file.peek();
      const auto limit = DeserializationOption::NestingLimit(nesting);
      if (c == '"' || c == '\'' || c == '[' || c == '{') {
        return !deserializeJson(doc, file, limit);
      }
      ScalarInput input{file};
      return !deserializeJson(doc, input, limit);
    }

    bool keyCharacter(int c) {
      return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= '_' && c <= 'z');
    }

    enum class Member { Other, Schema, Points };

    Member memberName(const char* name) {
      if (strcmp(name, "schema") == 0) return Member::Schema;
      if (strcmp(name, "points") == 0) return Member::Points;
      return Member::Other;
    }

    bool readMember(File& file, JsonDocument& scratch, Member& member) {
      skipWhitespace(file);
      if (file.peek() == '"' || file.peek() == '\'') {
        if (!readValue(file, scratch, 0)) return false;
        const char* name = scratch.as<const char*>();
        if (!name) return false;
        member = memberName(name);
      } else {
        char name[8] = {};
        size_t size = 0;
        while (keyCharacter(file.peek())) {
          const int c = file.read();
          if (size < sizeof(name)) name[size++] = static_cast<char>(c);
        }
        if (size == 0) return false;
        member = size == sizeof(name) ? Member::Other : memberName(name);
      }
      return consume(file, ':');
    }

    struct PointsLocation {
      size_t offset = 0;
      bool validPoint = false;
    };

    bool inspectDocument(File& file, PointsLocation& points, bool& supportedSchema) {
      if (!consume(file, '{')) return false;
      if (consume(file, '}')) return true;
      JsonDocument scratch;
      do {
        Member member;
        if (!readMember(file, scratch, member)) return false;
        if (member == Member::Points && consume(file, '[')) {
          points = {file.position(), false};
          if (consume(file, ']')) continue;
          do {
            if (!readValue(file, scratch, ARDUINOJSON_DEFAULT_NESTING_LIMIT - 2)) return false;
            if (validPoint(scratch.as<JsonObjectConst>())) points.validPoint = true;
          } while (consume(file, ','));
          if (!consume(file, ']')) return false;
        } else {
          if (!readValue(file, scratch, ARDUINOJSON_DEFAULT_NESTING_LIMIT - 1)) return false;
          if (member == Member::Schema && !scratch.isNull()) {
            const char* name = scratch.as<const char*>();
            supportedSchema = name && strcmp(name, "leaf.user_waypoints") == 0;
          }
          if (member == Member::Points && !scratch.isNull()) points = {};
        }
      } while (consume(file, ','));
      return consume(file, '}');
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
      waypoint.ele = point["alt_m"] | NAN;
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

  bool validPoint(JsonObjectConst point) {
    const double lat = point["lat"] | NAN;
    const double lon = point["lon"] | NAN;
    return !isnan(lat) && !isnan(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
  }

  bool Reader::open(String& error) {
    if (!SD_MMC.exists(filePath())) return true;
    file_ = SD_MMC.open(filePath(), "r");
    if (!file_) {
      error = "Unable to read user waypoints.";
      return false;
    }
    if (file_.size() > USER_WAYPOINTS_MAX_BYTES) {
      error = "User waypoint file is too large.";
      return false;
    }

    PointsLocation points;
    bool supportedSchema = false;
    if (!inspectDocument(file_, points, supportedSchema) ||
        (points.offset != 0 && !file_.seek(points.offset))) {
      error = "User waypoint file is invalid.";
      return false;
    }
    if (!supportedSchema) {
      error = "User waypoint file has an unsupported schema.";
      return false;
    }
    hasValidPoint_ = points.validPoint;
    readingPoints_ = points.offset != 0 && !consume(file_, ']');
    return true;
  }

  bool Reader::next(JsonDocument& point) {
    if (!readingPoints_) return false;
    if (!readValue(file_, point, ARDUINOJSON_DEFAULT_NESTING_LIMIT - 2)) {
      readingPoints_ = false;
      failed_ = true;
      return false;
    }
    readingPoints_ = consume(file_, ',');
    failed_ = !readingPoints_ && !consume(file_, ']');
    return !failed_;
  }

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
    Reader reader;
    String error;
    if (!reader.open(error)) return false;
    JsonDocument point;
    while (reader.next(point)) appendNavigatorWaypoint(point.as<JsonObjectConst>());
    return !reader.failed();
  }

  bool hasSavedPoints() {
    if (!sdcard.isMounted()) return false;
    Reader reader;
    String error;
    return reader.open(error) && reader.hasValidPoint();
  }

  bool loadAsNavigatorSource(bool persist) {
    heap_monitor::checkpoint("user-wpt-source-start");
    if (!sdcard.isMounted() || !SD_MMC.exists(filePath())) return false;
    Reader reader;
    String error;
    if (!reader.open(error)) return false;

    navigator.clear();
    JsonDocument doc;
    while (reader.next(doc)) {
      const JsonObjectConst point = doc.as<JsonObjectConst>();
      if (!validPoint(point)) continue;
      if (!appendNavigatorWaypoint(point)) {
        navigator.clear();
        return false;
      }
    }

    if (reader.failed() || navigator.totalWaypoints == 0) {
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
