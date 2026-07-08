#include "navigation/route_store.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "diagnostics/heap_monitor.h"
#include "navigation/gpx.h"

namespace route_store {
  namespace {
    constexpr size_t ROUTE_IMPORT_MAX_BYTES = 8192;
    constexpr size_t ROUTE_FILE_MAX_BYTES = 16384;
    constexpr const char* ROUTE_TEMP_FILE = "/routes/route.tmp";
    constexpr const char* ROUTE_BACKUP_FILE = "/routes/route.bak";

    String routeError(const char* message) { return String(message ? message : "Route error"); }

    String trimmed(String value) {
      value.trim();
      return value;
    }

    bool ensureRouteDirectory() {
      return SD_MMC.exists(directoryPath()) || SD_MMC.mkdir(directoryPath());
    }

    String safeRouteFileName(String name) {
      name.trim();
      if (name.isEmpty()) name = "route";

      String safe;
      safe.reserve(name.length());
      bool lastDash = false;
      for (uint16_t i = 0; i < name.length() && safe.length() < 48; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (valid) {
          safe += c;
          lastDash = false;
        } else if (!lastDash && safe.length() > 0) {
          safe += '-';
          lastDash = true;
        }
      }
      while (safe.endsWith("-")) safe.remove(safe.length() - 1);
      if (safe.isEmpty()) safe = "route";
      return String(directoryPath()) + "/" + safe + ".json";
    }

    const char* roleName(RoutePointRole role) {
      switch (role) {
        case RoutePointRole::Takeoff:
          return "takeoff";
        case RoutePointRole::StartSpeedSection:
          return "sss";
        case RoutePointRole::EndSpeedSection:
          return "ess";
        case RoutePointRole::EndSpeedSectionGoal:
          return "ess_goal";
        case RoutePointRole::Goal:
          return "goal";
        case RoutePointRole::Normal:
        default:
          return "normal";
      }
    }

    RoutePointRole roleFromName(const char* value) {
      if (value == nullptr) return RoutePointRole::Normal;
      if (strcmp(value, "takeoff") == 0) return RoutePointRole::Takeoff;
      if (strcmp(value, "sss") == 0) return RoutePointRole::StartSpeedSection;
      if (strcmp(value, "ess") == 0) return RoutePointRole::EndSpeedSection;
      if (strcmp(value, "ess_goal") == 0) return RoutePointRole::EndSpeedSectionGoal;
      if (strcmp(value, "goal") == 0) return RoutePointRole::Goal;
      return RoutePointRole::Normal;
    }

    const char* earthModelName(RouteEarthModel value) {
      return value == RouteEarthModel::FAISphere ? "fai_sphere" : "wgs84";
    }

    RouteEarthModel earthModelFromName(const char* value) {
      if (value != nullptr && strcmp(value, "fai_sphere") == 0) return RouteEarthModel::FAISphere;
      return RouteEarthModel::WGS84;
    }

    const char* goalTypeName(RouteGoalType value) {
      return value == RouteGoalType::Line ? "line" : "cylinder";
    }

    RouteGoalType goalTypeFromName(const char* value) {
      if (value != nullptr && strcmp(value, "line") == 0) return RouteGoalType::Line;
      return RouteGoalType::Cylinder;
    }

    const char* startTypeName(RouteStartType value) {
      switch (value) {
        case RouteStartType::Race:
          return "race";
        case RouteStartType::ElapsedTime:
          return "elapsed_time";
        case RouteStartType::None:
        default:
          return "none";
      }
    }

    RouteStartType startTypeFromName(const char* value) {
      if (value == nullptr) return RouteStartType::None;
      if (strcmp(value, "race") == 0) return RouteStartType::Race;
      if (strcmp(value, "elapsed_time") == 0) return RouteStartType::ElapsedTime;
      return RouteStartType::None;
    }

    uint16_t parseTimeMinutesUtc(const char* value, bool& valid) {
      valid = false;
      if (value == nullptr || strlen(value) < 5) return 0;
      if (!isdigit(value[0]) || !isdigit(value[1]) || value[2] != ':' || !isdigit(value[3]) ||
          !isdigit(value[4])) {
        return 0;
      }
      const uint8_t hours = (value[0] - '0') * 10 + (value[1] - '0');
      const uint8_t minutes = (value[3] - '0') * 10 + (value[4] - '0');
      if (hours > 23 || minutes > 59) return 0;
      valid = true;
      return hours * 60 + minutes;
    }

    String formatTimeMinutesUtc(uint16_t value) {
      char buffer[10];
      snprintf(buffer, sizeof(buffer), "%02u:%02u:00Z", value / 60, value % 60);
      return String(buffer);
    }

    bool decodePolylineValue(const char*& cursor, const char* end, int32_t& value) {
      int64_t result = 0;
      uint8_t shift = 0;
      uint8_t byte = 0;

      do {
        if (cursor >= end || shift > 30) return false;
        byte = static_cast<uint8_t>(*cursor++) - 63;
        result |= static_cast<int64_t>(byte & 0x1f) << shift;
        shift += 5;
      } while (byte >= 0x20);

      const int64_t decoded = (result & 1) ? ~(result >> 1) : (result >> 1);
      value = static_cast<int32_t>(decoded);
      return true;
    }

    bool decodeXctskZ(const char* z, double& lat, double& lon, float& altM, uint16_t& radiusM) {
      if (z == nullptr || z[0] == '\0') return false;
      const char* cursor = z;
      const char* end = z + strlen(z);
      int32_t lonE5 = 0;
      int32_t latE5 = 0;
      int32_t alt = 0;
      int32_t radius = 0;
      if (!decodePolylineValue(cursor, end, lonE5)) return false;
      if (!decodePolylineValue(cursor, end, latE5)) return false;
      if (!decodePolylineValue(cursor, end, alt)) return false;
      if (!decodePolylineValue(cursor, end, radius)) return false;
      if (cursor != end || radius < 0) return false;

      lon = lonE5 / 100000.0;
      lat = latE5 / 100000.0;
      altM = alt;
      radiusM = static_cast<uint16_t>(radius > 65535 ? 65535 : radius);
      return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
    }

    RoutePointRole compactRoleForPoint(JsonObjectConst point, bool lastPoint) {
      const int type = point["t"] | 0;
      if (type == 2) return RoutePointRole::StartSpeedSection;
      if (type == 3)
        return lastPoint ? RoutePointRole::EndSpeedSectionGoal : RoutePointRole::EndSpeedSection;
      return lastPoint ? RoutePointRole::Goal : RoutePointRole::Normal;
    }

    RoutePointRole fullRoleForPoint(JsonObjectConst point, bool lastPoint) {
      const char* type = point["type"] | "";
      if (strcmp(type, "TAKEOFF") == 0) return RoutePointRole::Takeoff;
      if (strcmp(type, "SSS") == 0) return RoutePointRole::StartSpeedSection;
      if (strcmp(type, "ESS") == 0) {
        return lastPoint ? RoutePointRole::EndSpeedSectionGoal : RoutePointRole::EndSpeedSection;
      }
      return lastPoint ? RoutePointRole::Goal : RoutePointRole::Normal;
    }

    bool normalizeCompactXctsk(JsonObjectConst input, JsonObject output, String& error) {
      JsonArrayConst turnpoints = input["t"].as<JsonArrayConst>();
      if (turnpoints.isNull() || turnpoints.size() == 0 || turnpoints.size() > maxRoutePointRefs) {
        error = routeError("Route has no usable turnpoints.");
        return false;
      }

      output["source_format"] = "xctsk";
      output["source_version"] = input["version"] | 2;
      output["task_type"] = "classic";
      output["earth_model"] = (input["e"] | 0) == 1 ? "fai_sphere" : "wgs84";

      JsonObject goal = output["goal"].to<JsonObject>();
      JsonObjectConst inputGoal = input["g"].as<JsonObjectConst>();
      goal["type"] = (inputGoal["t"] | 2) == 1 ? "line" : "cylinder";
      const char* deadline = inputGoal["d"] | "";
      if (deadline[0] != '\0') goal["deadline"] = deadline;

      JsonObject start = output["start"].to<JsonObject>();
      JsonObjectConst inputStart = input["s"].as<JsonObjectConst>();
      const int startType = inputStart["t"] | 0;
      start["type"] = startType == 1 ? "race" : startType == 2 ? "elapsed_time" : "none";
      JsonArray outGates = start["time_gates"].to<JsonArray>();
      for (JsonVariantConst gate : inputStart["g"].as<JsonArrayConst>()) {
        outGates.add(gate.as<const char*>());
      }

      JsonArray points = output["points"].to<JsonArray>();
      const uint8_t total = turnpoints.size();
      uint8_t index = 0;
      for (JsonObjectConst point : turnpoints) {
        index++;
        double lat = 0;
        double lon = 0;
        float altM = 0;
        uint16_t radiusM = defaultWaypointRadius;
        if (!decodeXctskZ(point["z"] | "", lat, lon, altM, radiusM)) {
          error = routeError("Route contains an invalid encoded turnpoint.");
          return false;
        }

        JsonObject outPoint = points.add<JsonObject>();
        outPoint["name"] = point["n"] | "";
        outPoint["lat"] = lat;
        outPoint["lon"] = lon;
        outPoint["alt_m"] = altM;
        outPoint["radius_m"] = radiusM;
        outPoint["role"] = roleName(compactRoleForPoint(point, index == total));
      }

      return true;
    }

    bool normalizeFullXctsk(JsonObjectConst input, JsonObject output, String& error) {
      JsonArrayConst turnpoints = input["turnpoints"].as<JsonArrayConst>();
      if (turnpoints.isNull() || turnpoints.size() == 0 || turnpoints.size() > maxRoutePointRefs) {
        error = routeError("Route has no usable turnpoints.");
        return false;
      }

      output["source_format"] = "xctsk";
      output["source_version"] = input["version"] | 1;
      output["task_type"] = "classic";
      const char* earthModel = input["earthModel"] | "WGS84";
      output["earth_model"] = strcmp(earthModel, "FAI_SPHERE") == 0 ? "fai_sphere" : "wgs84";

      JsonObject goal = output["goal"].to<JsonObject>();
      JsonObjectConst inputGoal = input["goal"].as<JsonObjectConst>();
      const char* goalType = inputGoal["type"] | "CYLINDER";
      goal["type"] = strcmp(goalType, "LINE") == 0 ? "line" : "cylinder";
      const char* deadline = inputGoal["deadline"] | "";
      if (deadline[0] != '\0') goal["deadline"] = deadline;

      JsonObject start = output["start"].to<JsonObject>();
      JsonObjectConst inputStart = input["sss"].as<JsonObjectConst>();
      const char* startType = inputStart["type"] | "";
      start["type"] = strcmp(startType, "RACE") == 0           ? "race"
                      : strcmp(startType, "ELAPSED-TIME") == 0 ? "elapsed_time"
                                                               : "none";
      JsonArray outGates = start["time_gates"].to<JsonArray>();
      for (JsonVariantConst gate : inputStart["timeGates"].as<JsonArrayConst>()) {
        outGates.add(gate.as<const char*>());
      }

      JsonArray points = output["points"].to<JsonArray>();
      const uint8_t total = turnpoints.size();
      uint8_t index = 0;
      for (JsonObjectConst point : turnpoints) {
        index++;
        JsonObjectConst waypoint = point["waypoint"].as<JsonObjectConst>();
        const double lat = waypoint["lat"] | NAN;
        const double lon = waypoint["lon"] | NAN;
        if (isnan(lat) || isnan(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) {
          error = routeError("Route contains an invalid waypoint coordinate.");
          return false;
        }

        JsonObject outPoint = points.add<JsonObject>();
        outPoint["name"] = waypoint["name"] | "";
        outPoint["lat"] = lat;
        outPoint["lon"] = lon;
        outPoint["alt_m"] = waypoint["altSmoothed"] | 0;
        outPoint["radius_m"] = point["radius"] | defaultWaypointRadius;
        outPoint["role"] = roleName(fullRoleForPoint(point, index == total));
      }

      return true;
    }

    bool normalizeRoute(String name, const String& data, JsonDocument& output, String& error) {
      if (data.length() == 0 || data.length() > ROUTE_IMPORT_MAX_BYTES) {
        error = routeError("Route data is empty or too large.");
        return false;
      }

      String payload = trimmed(data);
      if (payload.startsWith("XCTSKZ:")) {
        error = routeError("Compressed XCTSKZ routes are not supported yet.");
        return false;
      }
      if (payload.startsWith("XCTSK:")) {
        payload.remove(0, 6);
        payload.trim();
      }

      JsonDocument input;
      DeserializationError parseError = deserializeJson(input, payload);
      if (parseError) {
        error = routeError("Route data is not valid JSON.");
        return false;
      }

      JsonObject out = output.to<JsonObject>();
      out["schema"] = "leaf.route";
      out["schema_version"] = "v0.1.0";
      name.trim();
      out["name"] = name.isEmpty() ? "Imported Route" : name;

      JsonObjectConst in = input.as<JsonObjectConst>();
      const char* taskType = in["taskType"] | "";
      if (strcmp(taskType, "CLASSIC") != 0) {
        error = routeError("Only CLASSIC XCTSK tasks are supported.");
        return false;
      }

      if (!in["t"].isNull()) {
        return normalizeCompactXctsk(in, out, error);
      }
      if (!in["turnpoints"].isNull()) {
        return normalizeFullXctsk(in, out, error);
      }

      error = routeError("Route data does not contain turnpoints.");
      return false;
    }

    bool writeJsonFile(const String& path, JsonDocument& doc) {
      if (!ensureRouteDirectory()) return false;

      if (SD_MMC.exists(ROUTE_TEMP_FILE)) SD_MMC.remove(ROUTE_TEMP_FILE);
      if (SD_MMC.exists(ROUTE_BACKUP_FILE)) SD_MMC.remove(ROUTE_BACKUP_FILE);

      File file = SD_MMC.open(ROUTE_TEMP_FILE, "w", true);
      if (!file) return false;

      const size_t written = serializeJsonPretty(doc, file);
      file.close();
      if (written == 0) {
        SD_MMC.remove(ROUTE_TEMP_FILE);
        return false;
      }

      const bool hadExisting = SD_MMC.exists(path);
      if (hadExisting && !SD_MMC.rename(path, ROUTE_BACKUP_FILE)) {
        SD_MMC.remove(ROUTE_TEMP_FILE);
        return false;
      }
      if (!SD_MMC.rename(ROUTE_TEMP_FILE, path)) {
        if (hadExisting) SD_MMC.rename(ROUTE_BACKUP_FILE, path);
        SD_MMC.remove(ROUTE_TEMP_FILE);
        return false;
      }
      if (hadExisting) SD_MMC.remove(ROUTE_BACKUP_FILE);
      return true;
    }

    bool saveActiveRoutePointer(const String& path) {
      JsonDocument doc;
      doc["schema"] = "leaf.active_route";
      doc["schema_version"] = "v0.1.0";
      doc["path"] = path;
      return writeJsonFile(activeRoutePath(), doc);
    }

    bool loadNormalizedRoute(JsonDocument& doc, const String& path, bool activate) {
      navigator.clear();

      const char* schema = doc["schema"] | "";
      if (strcmp(schema, "leaf.route") != 0) return false;

      JsonArrayConst points = doc["points"].as<JsonArrayConst>();
      if (points.isNull() || points.size() == 0 || points.size() > maxRoutePointRefs) return false;

      Route route;
      route.setName(doc["name"] | "Route");
      route.taskType = RouteTaskType::Classic;
      route.earthModel = earthModelFromName(doc["earth_model"] | "wgs84");

      JsonObjectConst goal = doc["goal"].as<JsonObjectConst>();
      route.goalType = goalTypeFromName(goal["type"] | "cylinder");
      bool hasDeadline = false;
      route.goalDeadlineMinutesUtc = parseTimeMinutesUtc(goal["deadline"] | "", hasDeadline);
      route.hasGoalDeadline = hasDeadline;

      JsonObjectConst start = doc["start"].as<JsonObjectConst>();
      route.startType = startTypeFromName(start["type"] | "none");

      navigator.routes[1] = route;
      navigator.totalRoutes = 1;

      for (JsonObjectConst point : points) {
        Waypoint waypoint;
        waypoint.setName(point["name"] | "");
        waypoint.setLatitude(point["lat"] | 0.0);
        waypoint.setLongitude(point["lon"] | 0.0);
        waypoint.ele = point["alt_m"] | 0.0;

        WaypointID waypointIndex = navigator.addWaypoint(waypoint)
                                       ? WaypointID(navigator.totalWaypoints)
                                       : WaypointID::None;
        if (!waypointIndex) return false;

        const uint16_t radiusM = point["radius_m"] | defaultWaypointRadius;
        const RoutePointRole role = roleFromName(point["role"] | "normal");
        if (!navigator.addRoutePoint(&navigator.routes[1], waypointIndex, radiusM, role))
          return false;
      }

      navigator.setLoadedSavedRouteFilename(path);
      if (activate) navigator.activateRoute(RouteID(1));
      return true;
    }
  }  // namespace

  bool importRouteText(const String& name, const String& data, bool activate,
                       ImportResult& result) {
    heap_monitor::checkpoint("route-import-text");
    result = ImportResult();

    JsonDocument routeDoc;
    if (!normalizeRoute(name, data, routeDoc, result.error)) {
      heap_monitor::checkpoint("route-import-parse-fail");
      return false;
    }
    heap_monitor::checkpoint("route-import-normalized");

    const String path = safeRouteFileName(routeDoc["name"] | "route");
    if (!writeJsonFile(path, routeDoc)) {
      result.error = routeError("Route file could not be saved.");
      heap_monitor::checkpoint("route-import-write-fail");
      return false;
    }
    heap_monitor::checkpoint("route-import-written");

    result.path = path;
    result.points = routeDoc["points"].as<JsonArray>().size();
    result.ok = true;

    if (activate) {
      if (!saveActiveRoutePointer(path) || !loadNormalizedRoute(routeDoc, path, true)) {
        result.error = routeError("Route was saved but could not be activated.");
        heap_monitor::checkpoint("route-import-act-fail");
        return false;
      }
      result.active = true;
    }

    heap_monitor::checkpoint("route-import-done");
    return true;
  }

  bool saveRouteJson(JsonDocument& routeDoc, bool activate, ImportResult& result) {
    heap_monitor::checkpoint("route-save-json");
    result = ImportResult();

    JsonObject route = routeDoc.as<JsonObject>();
    route["schema"] = "leaf.route";
    route["schema_version"] = "v0.1.0";

    String name = route["name"] | "";
    name.trim();
    if (name.isEmpty()) name = "Web Route";
    route["name"] = name;

    JsonArray points = route["points"].as<JsonArray>();
    if (points.isNull() || points.size() == 0 || points.size() > maxRoutePointRefs) {
      result.error = routeError("Route has no usable turnpoints.");
      heap_monitor::checkpoint("route-save-invalid");
      return false;
    }

    const String path = safeRouteFileName(name);
    if (!writeJsonFile(path, routeDoc)) {
      result.error = routeError("Route file could not be saved.");
      heap_monitor::checkpoint("route-save-write-fail");
      return false;
    }
    heap_monitor::checkpoint("route-save-written");

    result.path = path;
    result.points = points.size();
    result.ok = true;

    if (activate) {
      if (!saveActiveRoutePointer(path) || !loadNormalizedRoute(routeDoc, path, true)) {
        result.error = routeError("Route was saved but could not be activated.");
        heap_monitor::checkpoint("route-save-act-fail");
        return false;
      }
      result.active = true;
    }

    heap_monitor::checkpoint("route-save-done");
    return true;
  }

  bool loadRouteFile(const String& path, bool activate) {
    heap_monitor::checkpoint("route-load-file");
    if (path.isEmpty() || path.length() > 96 || !SD_MMC.exists(path)) return false;

    File file = SD_MMC.open(path, "r");
    if (!file) return false;
    if (file.size() == 0 || file.size() > ROUTE_FILE_MAX_BYTES) {
      file.close();
      return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
      heap_monitor::checkpoint("route-load-json-fail");
      return false;
    }

    const bool loaded = loadNormalizedRoute(doc, path, activate);
    heap_monitor::checkpoint(loaded ? "route-load-done" : "route-load-fail");
    return loaded;
  }

  bool loadActiveRoute() {
    if (!SD_MMC.exists(activeRoutePath())) return false;

    File file = SD_MMC.open(activeRoutePath(), "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    const char* schema = doc["schema"] | "";
    if (strcmp(schema, "leaf.active_route") != 0) return false;

    const String path = doc["path"] | "";
    return loadRouteFile(path, true);
  }

  bool clearActiveRoute() {
    if (!SD_MMC.exists(activeRoutePath())) return true;
    return SD_MMC.remove(activeRoutePath());
  }

}  // namespace route_store
