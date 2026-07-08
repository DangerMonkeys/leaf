

// GPX is the common gps file format for storing waypoints, routes, and tracks, etc.
// This CPP file is for functions related to navigating and tracking active waypoints and routes
// (Though Leaf may/will support other file types in the future, we'll still use this gpx.cpp file
// even when dealing with other datatypes)

#include "navigation/gpx.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <SD_MMC.h>
#include <ctype.h>

#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logging/log.h"
#include "navigation/gpx_parser.h"
#include "navigation/route_store.h"
#include "navigation/user_waypoints.h"
#include "storage/files.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"

Navigator navigator;

namespace {
  constexpr size_t MAX_NAV_LINE_LENGTH = 256;
  constexpr size_t MAX_KML_PARSE_CHARS = 262144UL;
  constexpr const char* NAV_STATE_SCHEMA = "leaf.nav_state";
  constexpr const char* LEGACY_ACTIVE_ROUTE_SCHEMA = "leaf.active_route";

  String filenameFromPath(const String& path) {
    const int slash = path.lastIndexOf('/');
    if (slash < 0) return path;
    return path.substring(slash + 1);
  }

  String lowerExtension(const String& fileName) {
    const int dot = fileName.lastIndexOf('.');
    if (dot < 0) return "";
    String ext = fileName.substring(dot + 1);
    ext.toLowerCase();
    return ext;
  }

  const char* sourceName(LoadedNavSource source) {
    switch (source) {
      case LoadedNavSource::NavFile:
        return "nav_file";
      case LoadedNavSource::SavedRoute:
        return "saved_route";
      case LoadedNavSource::UserWaypoints:
        return "user_waypoints";
      case LoadedNavSource::None:
      default:
        return "none";
    }
  }

  LoadedNavSource sourceFromName(const char* value) {
    if (value == nullptr) return LoadedNavSource::None;
    if (strcmp(value, "nav_file") == 0) return LoadedNavSource::NavFile;
    if (strcmp(value, "saved_route") == 0) return LoadedNavSource::SavedRoute;
    if (strcmp(value, "user_waypoints") == 0) return LoadedNavSource::UserWaypoints;
    return LoadedNavSource::None;
  }

  bool writeNavState(JsonDocument& doc) {
    if (!SD_MMC.exists(route_store::directoryPath()) && !SD_MMC.mkdir(route_store::directoryPath()))
      return false;

    const char* tempPath = "/routes/active.tmp";
    if (SD_MMC.exists(tempPath)) SD_MMC.remove(tempPath);

    File file = SD_MMC.open(tempPath, "w", true);
    if (!file) return false;

    const size_t written = serializeJsonPretty(doc, file);
    file.close();
    if (written == 0) {
      SD_MMC.remove(tempPath);
      return false;
    }

    SD_MMC.remove(route_store::activeRoutePath());
    if (!SD_MMC.rename(tempPath, route_store::activeRoutePath())) {
      SD_MMC.remove(tempPath);
      return false;
    }
    return true;
  }

  bool readLine(File& file, char* buffer, size_t len) {
    if (!file || file.available() <= 0 || len == 0) return false;

    size_t index = 0;
    while (file.available() > 0) {
      const char c = file.read();
      if (c == '\r') continue;
      if (c == '\n') break;
      if (index + 1 < len) buffer[index++] = c;
    }
    buffer[index] = '\0';
    return true;
  }

  char* trimInPlace(char* value) {
    if (value == nullptr) return value;
    while (*value == ' ' || *value == '\t') value++;
    char* end = value + strlen(value);
    while (end > value && (end[-1] == ' ' || end[-1] == '\t')) {
      *--end = '\0';
    }
    return value;
  }

  bool nextCsvField(const char*& cursor, char* out, size_t outLen) {
    if (cursor == nullptr || *cursor == '\0' || outLen == 0) return false;

    size_t index = 0;
    bool quoted = false;
    if (*cursor == '"') {
      quoted = true;
      cursor++;
    }

    while (*cursor != '\0') {
      const char c = *cursor++;
      if (quoted) {
        if (c == '"') {
          if (*cursor == '"') {
            if (index + 1 < outLen) out[index++] = '"';
            cursor++;
          } else {
            quoted = false;
          }
        } else if (index + 1 < outLen) {
          out[index++] = c;
        }
      } else if (c == ',') {
        break;
      } else if (index + 1 < outLen) {
        out[index++] = c;
      }
    }

    out[index] = '\0';
    return true;
  }

  bool csvFieldAt(const char* line, uint8_t target, char* out, size_t outLen) {
    const char* cursor = line;
    char field[96];
    for (uint8_t i = 0; i <= target; i++) {
      if (!nextCsvField(cursor, field, sizeof(field))) return false;
    }
    strncpy(out, trimInPlace(field), outLen);
    out[outLen - 1] = '\0';
    return true;
  }

  bool parseCupCoordinate(const char* text, double& value) {
    if (text == nullptr) return false;
    char buffer[32];
    strncpy(buffer, text, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';
    char* s = trimInPlace(buffer);
    const size_t len = strlen(s);
    if (len < 5) return false;

    const char hemi = s[len - 1];
    if (hemi != 'N' && hemi != 'S' && hemi != 'E' && hemi != 'W' && hemi != 'n' && hemi != 's' &&
        hemi != 'e' && hemi != 'w') {
      return false;
    }
    s[len - 1] = '\0';
    char* dot = strchr(s, '.');
    if (dot == nullptr || dot - s < 2) return false;

    const uint8_t degreeDigits = (dot - s) - 2;
    if (degreeDigits < 2 || degreeDigits > 3) return false;

    char degreesText[4] = "";
    strncpy(degreesText, s, degreeDigits);
    degreesText[degreeDigits] = '\0';
    const double degrees = atof(degreesText);
    const double minutes = atof(s + degreeDigits);
    if (minutes < 0 || minutes >= 60) return false;

    value = degrees + minutes / 60.0;
    if (hemi == 'S' || hemi == 'W' || hemi == 's' || hemi == 'w') value = -value;
    return true;
  }

  float parseElevationMeters(const char* text) {
    if (text == nullptr) return 0;
    return atof(text);
  }

  float parseElevationFeetAsMeters(const char* text) {
    if (text == nullptr) return 0;
    return atof(text) * 0.3048f;
  }

  bool validCoordinate(double lat, double lon) {
    return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
  }

  bool addWaypoint(Navigator* result, const char* name, double lat, double lon, float ele) {
    if (!validCoordinate(lat, lon)) return false;

    Waypoint waypoint;
    waypoint.setName(name);
    waypoint.setCoordinates(lat, lon);
    waypoint.ele = ele;
    return result->addWaypoint(waypoint);
  }

  bool addOrFindRouteWaypoint(Navigator* result, Route* route, const char* name, double lat,
                              double lon, float ele) {
    if (!validCoordinate(lat, lon)) return false;

    Waypoint waypoint;
    waypoint.setName(name);
    waypoint.setCoordinates(lat, lon);
    waypoint.ele = ele;
    const WaypointID waypointIndex = result->addOrFindWaypoint(waypoint);
    return waypointIndex && result->addRoutePoint(route, waypointIndex);
  }

  bool parseCupFile(fs::FS& fs, const String& fileName, Navigator* result) {
    File file = fs.open(fileName, FILE_READ);
    if (!file) return false;

    char line[MAX_NAV_LINE_LENGTH];
    bool parsedAny = false;
    bool inTasks = false;
    uint16_t linesRead = 0;

    while (readLine(file, line, sizeof(line))) {
      if ((++linesRead & 0x0F) == 0) yield();
      char* trimmed = trimInPlace(line);
      if (trimmed[0] == '\0') continue;
      if (strstr(trimmed, "-----Related Tasks-----") != nullptr) {
        inTasks = true;
        continue;
      }

      if (!inTasks) {
        char name[64];
        char latText[32];
        char lonText[32];
        char eleText[32];
        if (!csvFieldAt(trimmed, 0, name, sizeof(name)) || strcmp(name, "name") == 0 ||
            strcmp(name, "Title") == 0 || !csvFieldAt(trimmed, 3, latText, sizeof(latText)) ||
            !csvFieldAt(trimmed, 4, lonText, sizeof(lonText)) ||
            !csvFieldAt(trimmed, 5, eleText, sizeof(eleText))) {
          continue;
        }

        double lat = 0;
        double lon = 0;
        if (parseCupCoordinate(latText, lat) && parseCupCoordinate(lonText, lon) &&
            addWaypoint(result, name, lat, lon, parseElevationMeters(eleText))) {
          parsedAny = true;
        }
      } else {
        char field[64];
        const char* cursor = trimmed;
        if (!nextCsvField(cursor, field, sizeof(field)) || field[0] == '\0') continue;
        if (result->totalRoutes >= maxRoutes) continue;

        Route* activeRoute = &result->routes[++result->totalRoutes];
        activeRoute->setName(field);
        while (nextCsvField(cursor, field, sizeof(field))) {
          char* wpName = trimInPlace(field);
          if (wpName[0] == '\0') continue;
          const WaypointID waypointIndex = result->findWaypointByName(wpName);
          if (waypointIndex) result->addRoutePoint(activeRoute, waypointIndex);
        }
        if (activeRoute->totalPoints == 0) result->totalRoutes--;
      }
    }

    file.close();
    return parsedAny;
  }

  bool parseWptFile(fs::FS& fs, const String& fileName, Navigator* result) {
    File file = fs.open(fileName, FILE_READ);
    if (!file) return false;

    char line[MAX_NAV_LINE_LENGTH];
    bool parsedAny = false;
    uint16_t linesRead = 0;
    while (readLine(file, line, sizeof(line))) {
      if ((++linesRead & 0x0F) == 0) yield();
      char* trimmed = trimInPlace(line);
      if (trimmed[0] == '\0' || strstr(trimmed, "OziExplorer") == trimmed) continue;

      char name[64];
      char latText[32];
      char lonText[32];
      char eleText[32] = "0";
      double lat = 0;
      double lon = 0;
      float ele = 0;

      if (csvFieldAt(trimmed, 1, name, sizeof(name)) &&
          csvFieldAt(trimmed, 2, latText, sizeof(latText)) &&
          csvFieldAt(trimmed, 3, lonText, sizeof(lonText))) {
        csvFieldAt(trimmed, 14, eleText, sizeof(eleText));
        lat = atof(latText);
        lon = atof(lonText);
        ele = parseElevationFeetAsMeters(eleText);
      } else if (csvFieldAt(trimmed, 0, name, sizeof(name)) &&
                 csvFieldAt(trimmed, 1, latText, sizeof(latText)) &&
                 csvFieldAt(trimmed, 2, lonText, sizeof(lonText))) {
        csvFieldAt(trimmed, 3, eleText, sizeof(eleText));
        lat = atof(latText);
        lon = atof(lonText);
        ele = parseElevationMeters(eleText);
      } else {
        continue;
      }

      if (addWaypoint(result, name, lat, lon, ele)) parsedAny = true;
    }

    file.close();
    return parsedAny;
  }

  bool parseKmlCoordinateTuple(const char* tuple, double& lat, double& lon, float& ele) {
    if (tuple == nullptr) return false;
    char buffer[96];
    strncpy(buffer, tuple, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';
    char* lonText = trimInPlace(buffer);
    char* latText = strchr(lonText, ',');
    if (latText == nullptr) return false;
    *latText++ = '\0';
    char* eleText = strchr(latText, ',');
    if (eleText != nullptr) *eleText++ = '\0';

    lon = atof(lonText);
    lat = atof(latText);
    ele = eleText == nullptr ? 0 : atof(eleText);
    return validCoordinate(lat, lon);
  }

  bool parseKmlFile(fs::FS& fs, const String& fileName, Navigator* result) {
    File file = fs.open(fileName, FILE_READ);
    if (!file) return false;
    if (file.size() > MAX_KML_PARSE_CHARS) {
      file.close();
      return false;
    }

    String content;
    content.reserve(file.size() + 1);
    while (file.available() > 0) {
      content += static_cast<char>(file.read());
    }
    file.close();

    bool parsedAny = false;
    int searchFrom = 0;
    while (true) {
      const int placemarkStart = content.indexOf("<Placemark", searchFrom);
      if (placemarkStart < 0) break;
      const int placemarkEnd = content.indexOf("</Placemark>", placemarkStart);
      if (placemarkEnd < 0) break;

      const String placemark = content.substring(placemarkStart, placemarkEnd);
      String name = "KML Point";
      const int nameStart = placemark.indexOf("<name>");
      const int nameEnd = nameStart >= 0 ? placemark.indexOf("</name>", nameStart) : -1;
      if (nameStart >= 0 && nameEnd > nameStart) {
        name = placemark.substring(nameStart + 6, nameEnd);
        name.trim();
      }

      const int coordinatesStart = placemark.indexOf("<coordinates>");
      const int coordinatesEnd =
          coordinatesStart >= 0 ? placemark.indexOf("</coordinates>", coordinatesStart) : -1;
      if (coordinatesStart >= 0 && coordinatesEnd > coordinatesStart) {
        String coordinates = placemark.substring(coordinatesStart + 13, coordinatesEnd);
        coordinates.trim();
        const bool isRoute = placemark.indexOf("<LineString") >= 0;

        if (isRoute && result->totalRoutes < maxRoutes) {
          Route* route = &result->routes[++result->totalRoutes];
          route->setName(name.c_str());
          uint8_t routeIndex = 1;
          int tupleStart = 0;
          while (tupleStart < coordinates.length()) {
            while (tupleStart < coordinates.length() && isspace(coordinates[tupleStart]))
              tupleStart++;
            int tupleEnd = tupleStart;
            while (tupleEnd < coordinates.length() && !isspace(coordinates[tupleEnd])) tupleEnd++;
            String tuple = coordinates.substring(tupleStart, tupleEnd);
            double lat = 0;
            double lon = 0;
            float ele = 0;
            char pointName[maxGpxNameLength + 1];
            snprintf(pointName, sizeof(pointName), "%s %u", name.c_str(), routeIndex++);
            if (parseKmlCoordinateTuple(tuple.c_str(), lat, lon, ele) &&
                addOrFindRouteWaypoint(result, route, pointName, lat, lon, ele)) {
              parsedAny = true;
            }
            tupleStart = tupleEnd + 1;
          }
          if (route->totalPoints == 0) result->totalRoutes--;
        } else {
          double lat = 0;
          double lon = 0;
          float ele = 0;
          if (parseKmlCoordinateTuple(coordinates.c_str(), lat, lon, ele) &&
              addWaypoint(result, name.c_str(), lat, lon, ele)) {
            parsedAny = true;
          }
        }
      }

      searchFrom = placemarkEnd + 12;
    }

    return parsedAny;
  }
}  // namespace

void Navigator::init() {
  clear();

  // loadWaypoints();
  // loadRoutes();
  // activatePoint(19);
  // activateRoute(3);
}

void Navigator::clear() {
  totalWaypoints = 0;
  totalRoutePointRefs = 0;
  totalRoutes = 0;
  activePoint = Waypoint();
  activeRoutePoint = RoutePoint();
  activeWaypointIndex = WaypointID::None;
  activeRoutePointIndex = RouteIndex::None;
  activeRouteIndex = RouteID::None;
  altAboveWaypoint = 0;
  averageSpeed = 0;
  glideToActive = 0;
  segmentDistance = 0;
  pointDistanceRemaining = 0;
  pointTimeRemaining = 0;
  turnToActive = 0;
  navigating = false;
  nextPoint_ = Waypoint();
  goalPoint_ = Waypoint();
  nextPointIndex_ = RouteIndex::None;
  altAboveGoal_ = 0;
  glideToGoal_ = 0;
  totalDistanceRemaining_ = 0;
  courseToActive_ = 0;
  courseToNext_ = 0;
  turnToNext_ = 0;
  reachedGoal_ = false;
  loadedFileWaypointCount_ = 0;
  loadedNavSource_ = LoadedNavSource::None;
  loadedGpxFilename_[0] = '\0';
  loadedNavPath_[0] = '\0';
  lastNavType_ = LastNavType::None;
  lastRouteIndex_ = RouteID::None;
  lastRoutePointIndex_ = RouteIndex::None;
  lastWaypointIndex_ = WaypointID::None;
}

bool Navigator::addWaypoint(const Waypoint& waypoint) {
  if (totalWaypoints >= maxNavPoints) {
    return false;
  }
  waypoints[++totalWaypoints] = waypoint;
  return true;
}

WaypointID Navigator::findWaypointByName(const char* name) const {
  if (name == nullptr || name[0] == '\0') {
    return WaypointID::None;
  }
  for (uint8_t i = 1; i <= totalWaypoints; i++) {
    if (strncmp(waypoints[i].name, name, maxGpxNameLength) == 0) {
      return WaypointID(i);
    }
  }
  return WaypointID::None;
}

WaypointID Navigator::addOrFindWaypoint(const Waypoint& waypoint) {
  WaypointID existingIndex = findWaypointByName(waypoint.name);
  if (existingIndex) {
    return existingIndex;
  }
  if (!addWaypoint(waypoint)) {
    return WaypointID::None;
  }
  return WaypointID(totalWaypoints);
}

bool Navigator::addRoutePoint(Route* route, WaypointID waypointIndex, uint16_t radiusM,
                              RoutePointRole role) {
  if (!waypointIndex || totalRoutePointRefs >= maxRoutePointRefs) {
    return false;
  }
  if (route->totalPoints == 0) {
    route->firstRoutePointIndex = totalRoutePointRefs + 1;
  }
  RoutePoint routePoint;
  routePoint.waypointIndex = waypointIndex;
  routePoint.radiusM = radiusM == 0 ? defaultWaypointRadius : radiusM;
  routePoint.role = role;
  routePoints[++totalRoutePointRefs] = routePoint;
  route->totalPoints++;
  return true;
}

const Waypoint& Navigator::waypoint(WaypointID pointIndex) const { return waypoints[pointIndex]; }

const Waypoint& Navigator::routePoint(RouteID routeIndex, RouteIndex pointIndex) const {
  return waypoint(routePointMeta(routeIndex, pointIndex).waypointIndex);
}

const RoutePoint& Navigator::routePointMeta(RouteID routeIndex, RouteIndex pointIndex) const {
  return routePoints[routes[routeIndex].firstRoutePointIndex + pointIndex - 1];
}

// update nav data every second
void Navigator::update() {
  if (hasActivePoint() && !gpsPositionUsable()) {
    clearNavSolution();
  }

  // only update nav info if we're tracking to an active point
  if (hasNavSolution()) {
    // update distance remaining, then sequence to next point if distance is small enough
    pointDistanceRemaining = gps.distanceBetween(gps.location.lat(), gps.location.lng(),
                                                 activePoint.latitude(), activePoint.longitude());
    if (segmentDistance <= 0) segmentDistance = pointDistanceRemaining;
    const uint16_t activeRadius =
        activeRouteIndex ? activeRoutePoint.radiusM : defaultWaypointRadius;
    if (pointDistanceRemaining < activeRadius && !reachedGoal_ && flightTimer_isLogging())
      sequenceWaypoint();  //  (this will also update distance to the new point)

    // update time remaining
    if (!gps.hasFreshGroundSpeed() || gps.speed.mps() < 0.5) {
      pointTimeRemaining = 0;
    } else {
      const double distanceToCylinder =
          pointDistanceRemaining > activeRadius ? pointDistanceRemaining - activeRadius : 0;
      pointTimeRemaining = distanceToCylinder / gps.speed.mps();
    }

    // get degress to active point
    courseToActive_ = gps.courseTo(gps.location.lat(), gps.location.lng(), activePoint.latitude(),
                                   activePoint.longitude());
    turnToActive = courseToActive_ - gps.course.deg();
    if (turnToActive > 180)
      turnToActive -= 360;
    else if (turnToActive < -180)
      turnToActive += 360;

    // if there's a next point, get course to that as well
    if (nextPointIndex_) {
      courseToNext_ = gps.courseTo(gps.location.lat(), gps.location.lng(), nextPoint_.latitude(),
                                   nextPoint_.longitude());
      turnToNext_ = courseToNext_ - gps.course.deg();
      if (turnToNext_ > 180)
        turnToNext_ -= 360;
      else if (turnToNext_ < -180)
        turnToNext_ += 360;
    }

    // get glide to active (and goal point, if we're on a route)
    glideToActive = pointDistanceRemaining / (gps.altitude.meters() - activePoint.ele);
    if (activeRouteIndex)
      glideToGoal_ = totalDistanceRemaining_ / (gps.altitude.meters() - goalPoint_.ele);

    // update relative altimeters (in cm)
    // alt above active point
    altAboveWaypoint = 100 * (gps.altitude.meters() - activePoint.ele);

    // alt above goal (if we're on a route and have a goal; otherwise, set relative goal alt to same
    // as next point)
    if (activeRouteIndex)
      altAboveGoal_ = 100 * (gps.altitude.meters() - goalPoint_.ele);
    else
      altAboveGoal_ = altAboveWaypoint;
  }

  // update additional values that are required regardless of if we're navigating to a point
  // average speed
  if (gps.hasFreshGroundSpeed()) {
    averageSpeed =
        (averageSpeed * (AVERAGE_SPEED_SAMPLES - 1) + gps.speed.kmph()) / AVERAGE_SPEED_SAMPLES;
  }
}

// Start, Sequence, and End Navigation Functions

bool Navigator::activatePoint(WaypointID pointIndex) { return activatePoint(pointIndex, true); }

bool Navigator::activatePoint(WaypointID pointIndex, bool playSound) {
  if (!pointIndex || pointIndex > totalWaypoints) return false;

  navigating = true;
  reachedGoal_ = false;
  lastNavType_ = LastNavType::Point;
  lastRouteIndex_ = RouteID::None;
  lastRoutePointIndex_ = RouteIndex::None;
  lastWaypointIndex_ = pointIndex;

  // Point navigation is exclusive from Route navigation, so cancel any Route navigation
  activeRouteIndex = RouteID::None;
  activeRoutePointIndex = RouteIndex::None;

  activeWaypointIndex = pointIndex;
  activePoint = waypoint(activeWaypointIndex);
  activeRoutePoint = RoutePoint();

  if (playSound) speaker.playSound(fx::enter);

  if (gpsPositionUsable()) {
    double newDistance = gps.distanceBetween(gps.location.lat(), gps.location.lng(),
                                             activePoint.latitude(), activePoint.longitude());

    segmentDistance = newDistance;
    totalDistanceRemaining_ = newDistance;
    pointDistanceRemaining = newDistance;
  } else {
    segmentDistance = 0;
    totalDistanceRemaining_ = 0;
    clearNavSolution();
  }

  savePersistedState();
  return navigating;
}

bool Navigator::activateRoute(RouteID routeIndex) { return activateRoute(routeIndex, true); }

bool Navigator::activateRoute(RouteID routeIndex, bool playSound) {
  return activateRoute(routeIndex, RouteIndex(1), playSound);
}

bool Navigator::activateRoute(RouteID routeIndex, RouteIndex routePointIndex) {
  return activateRoute(routeIndex, routePointIndex, true);
}

bool Navigator::activateRoute(RouteID routeIndex, RouteIndex routePointIndex, bool playSound) {
  if (!routeIndex || routeIndex > totalRoutes) return false;

  // first check if any valid points
  uint8_t validPoints = routes[routeIndex].totalPoints;
  if (routePointIndex < 1 || routePointIndex > validPoints) return false;
  if (!validPoints) {
    navigating = false;
  } else {
    navigating = true;
    reachedGoal_ = false;
    activeRouteIndex = routeIndex;
    activeWaypointIndex = WaypointID::None;
    lastNavType_ = LastNavType::Route;
    lastRouteIndex_ = routeIndex;
    lastRoutePointIndex_ = routePointIndex;
    lastWaypointIndex_ = WaypointID::None;

    Serial.print("*** NEW ROUTE: ");
    Serial.println(routes[activeRouteIndex].name);

    // set activePointIndex to one before the desired point, then call sequenceWaypoint() to
    // increment and populate new
    // activePoint, and nextPoint, if any
    activeRoutePointIndex = routePointIndex - 1;
    sequenceWaypoint(playSound);
    if (!gpsPositionUsable()) clearNavSolution();

    // calculate TOTAL Route distance
    totalDistanceRemaining_ = 0;
    // if we have at least 2 points:
    if (routes[activeRouteIndex].totalPoints >= 2) {
      for (int i = 1; i < routes[activeRouteIndex].totalPoints; i++) {
        totalDistanceRemaining_ +=
            gps.distanceBetween(routePoint(activeRouteIndex, RouteIndex(i)).latitude(),
                                routePoint(activeRouteIndex, RouteIndex(i)).longitude(),
                                routePoint(activeRouteIndex, RouteIndex(i + 1)).latitude(),
                                routePoint(activeRouteIndex, RouteIndex(i + 1)).longitude());
      }
      // otherwise our Route only has 1 point, so the Route distance is from where we are now to
      // that one point
    } else if (routes[activeRouteIndex].totalPoints == 1) {
      totalDistanceRemaining_ =
          gpsPositionUsable()
              ? gps.distanceBetween(gps.location.lat(), gps.location.lng(),
                                    routePoint(activeRouteIndex, RouteIndex(1)).latitude(),
                                    routePoint(activeRouteIndex, RouteIndex(1)).longitude())
              : 0;
    }
  }
  savePersistedState();
  return navigating;
}

bool Navigator::sequenceWaypoint(bool playSound) {
  Serial.print("entering sequence..");

  bool successfulSequence = false;

  // sequence to next point if we're on a route && there's another point in the Route
  if (activeRouteIndex && activeRoutePointIndex < routes[activeRouteIndex].totalPoints) {
    successfulSequence = true;

    // TODO: play going to next point sound, or whatever
    if (playSound) speaker.playSound(fx::enter);

    activeRoutePointIndex++;
    lastNavType_ = LastNavType::Route;
    lastRouteIndex_ = activeRouteIndex;
    lastRoutePointIndex_ = activeRoutePointIndex;
    lastWaypointIndex_ = WaypointID::None;
    Serial.print(" new active index:");
    Serial.print(activeRoutePointIndex);
    Serial.print(" route index:");
    Serial.print(activeRouteIndex);
    activeRoutePoint = routePointMeta(activeRouteIndex, activeRoutePointIndex);
    activePoint = waypoint(activeRoutePoint.waypointIndex);

    Serial.print(" new point:");
    Serial.print(activePoint.name);
    Serial.print(" new lat: ");
    Serial.print(activePoint.latitude());

    if (activeRoutePointIndex + 1 <
        routes[activeRouteIndex]
            .totalPoints) {  // if there's also a next point in the list, capture that
      nextPointIndex_ = activeRoutePointIndex + 1;
      nextPoint_ = routePoint(activeRouteIndex, nextPointIndex_);
    } else {  // otherwise signify no next point, so we don't show display functions related to next
              // point
      nextPointIndex_ = RouteIndex::NoNextPoint;
    }

    // get distance between present (prev) point and new activePoint (used for distance progress
    // bar) if we're sequencing to the very first point, then there's no previous point to use, so
    // use our current location instead
    if (activeRoutePointIndex == 1) {
      segmentDistance =
          gpsPositionUsable()
              ? gps.distanceBetween(gps.location.lat(), gps.location.lng(),
                                    routePoint(activeRouteIndex, activeRoutePointIndex).latitude(),
                                    routePoint(activeRouteIndex, activeRoutePointIndex).longitude())
              : 0;
    } else {
      segmentDistance =
          gps.distanceBetween(routePoint(activeRouteIndex, activeRoutePointIndex - 1).latitude(),
                              routePoint(activeRouteIndex, activeRoutePointIndex - 1).longitude(),
                              routePoint(activeRouteIndex, activeRoutePointIndex).latitude(),
                              routePoint(activeRouteIndex, activeRoutePointIndex).longitude());
    }

  } else {  // otherwise, we made it to our destination!
    // TODO: celebrate!  (play reaching goal sound, or whatever)
    reachedGoal_ = true;
    if (playSound) speaker.playSound(fx::confirm);
  }
  if (successfulSequence) savePersistedState();
  Serial.print(" succes is: ");
  Serial.println(successfulSequence);
  return successfulSequence;
}

void Navigator::cancelNav() {
  pointDistanceRemaining = 0;
  pointTimeRemaining = 0;
  activeRouteIndex = RouteID::None;
  activeWaypointIndex = WaypointID::None;
  activeRoutePointIndex = RouteIndex::None;
  activeRoutePoint = RoutePoint();
  reachedGoal_ = false;
  navigating = false;
  turnToActive = 0;
  turnToNext_ = 0;
  savePersistedState();
  speaker.playSound(fx::cancel);
}

bool Navigator::hasActivePoint() const { return activeWaypointIndex || activeRoutePointIndex; }

bool Navigator::gpsPositionUsable() const { return gps.hasUsableFix(); }

bool Navigator::hasNavSolution() const { return hasActivePoint() && gpsPositionUsable(); }

void Navigator::clearNavSolution() {
  pointDistanceRemaining = 0;
  pointTimeRemaining = 0;
  turnToActive = 0;
  turnToNext_ = 0;
  glideToActive = 0;
}

const char* Navigator::lastNavDestinationName() const {
  if (lastNavType_ == LastNavType::Route && lastRouteIndex_ && lastRouteIndex_ <= totalRoutes)
    return routes[lastRouteIndex_].name;
  if (lastNavType_ == LastNavType::Point && lastWaypointIndex_ &&
      lastWaypointIndex_ <= totalWaypoints)
    return waypoint(lastWaypointIndex_).name;
  return "";
}

RouteID Navigator::routeContextIndex() const {
  if (activeRouteIndex) return activeRouteIndex;
  if (hasActivePoint()) return RouteID::None;
  if (lastNavType_ == LastNavType::Route && lastRouteIndex_ && lastRouteIndex_ <= totalRoutes)
    return lastRouteIndex_;
  if (totalRoutes == 1) return RouteID(1);
  return RouteID::None;
}

void Navigator::setLoadedGpxFilename(const String& fileName) {
  String baseName = filenameFromPath(fileName);
  baseName.toCharArray(loadedGpxFilename_, sizeof(loadedGpxFilename_));
  loadedGpxFilename_[maxGpxFileNameLength] = '\0';
  fileName.toCharArray(loadedNavPath_, sizeof(loadedNavPath_));
  loadedNavPath_[maxGpxFileNameLength] = '\0';
  markLoadedFileWaypointCount();
  loadedNavSource_ = LoadedNavSource::NavFile;
}

void Navigator::setLoadedSavedRouteFilename(const String& fileName) {
  String baseName = filenameFromPath(fileName);
  baseName.toCharArray(loadedGpxFilename_, sizeof(loadedGpxFilename_));
  loadedGpxFilename_[maxGpxFileNameLength] = '\0';
  fileName.toCharArray(loadedNavPath_, sizeof(loadedNavPath_));
  loadedNavPath_[maxGpxFileNameLength] = '\0';
  markLoadedFileWaypointCount();
  loadedNavSource_ = LoadedNavSource::SavedRoute;
}

void Navigator::setLoadedUserWaypointsFilename(const String& fileName) {
  strncpy(loadedGpxFilename_, "User Waypoints", sizeof(loadedGpxFilename_));
  loadedGpxFilename_[maxGpxFileNameLength] = '\0';
  fileName.toCharArray(loadedNavPath_, sizeof(loadedNavPath_));
  loadedNavPath_[maxGpxFileNameLength] = '\0';
  markLoadedFileWaypointCount();
  loadedNavSource_ = LoadedNavSource::UserWaypoints;
}

bool Navigator::savePersistedState() {
  if (!sdcard.isMounted() || loadedNavSource_ == LoadedNavSource::None || loadedNavPath_[0] == '\0')
    return false;

  JsonDocument doc;
  doc["schema"] = NAV_STATE_SCHEMA;
  doc["schema_version"] = "v0.1.0";
  JsonObject source = doc["source"].to<JsonObject>();
  source["type"] = sourceName(loadedNavSource_);
  source["path"] = loadedNavPath_;

  JsonObject active = doc["active"].to<JsonObject>();
  if (activeRouteIndex) {
    active["type"] = "route";
    active["index"] = static_cast<uint8_t>(activeRouteIndex);
    active["route_point_index"] = static_cast<int16_t>(activeRoutePointIndex);
    active["route_complete"] = reachedGoal_;
  } else if (activeWaypointIndex) {
    active["type"] = "point";
    active["index"] = static_cast<uint8_t>(activeWaypointIndex);
  } else if (lastNavType_ == LastNavType::Route) {
    active["type"] = "route";
    active["index"] = static_cast<uint8_t>(lastRouteIndex_);
    active["route_point_index"] = static_cast<int16_t>(lastRoutePointIndex_);
    active["route_complete"] = reachedGoal_;
  } else if (lastNavType_ == LastNavType::Point) {
    active["type"] = "point";
    active["index"] = static_cast<uint8_t>(lastWaypointIndex_);
  } else {
    active["type"] = "none";
    active["index"] = 0;
  }

  return writeNavState(doc);
}

bool Navigator::clearPersistedState() {
  if (!sdcard.isMounted() || !SD_MMC.exists(route_store::activeRoutePath())) return true;
  return SD_MMC.remove(route_store::activeRoutePath());
}

bool Navigator::loadPersistedState() { return loadPersistedState(true); }

bool Navigator::loadPersistedState(bool activate) {
  if (!sdcard.isMounted() || !SD_MMC.exists(route_store::activeRoutePath())) return false;

  File file = SD_MMC.open(route_store::activeRoutePath(), "r");
  if (!file) return false;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) return false;

  const char* schema = doc["schema"] | "";
  if (strcmp(schema, LEGACY_ACTIVE_ROUTE_SCHEMA) == 0) {
    const String path = doc["path"] | "";
    if (!route_store::loadRouteFile(path, false)) return false;
    lastNavType_ = LastNavType::Route;
    lastRouteIndex_ = RouteID(1);
    lastRoutePointIndex_ = RouteIndex(1);
    lastWaypointIndex_ = WaypointID::None;
    return activate ? activateRoute(RouteID(1), false) : true;
  }
  if (strcmp(schema, NAV_STATE_SCHEMA) != 0) return false;

  JsonObjectConst source = doc["source"].as<JsonObjectConst>();
  const LoadedNavSource sourceType = sourceFromName(source["type"] | "");
  const String path = source["path"] | "";
  if (sourceType == LoadedNavSource::None || path.isEmpty()) return false;

  bool loaded = false;
  if (sourceType == LoadedNavSource::SavedRoute) {
    loaded = route_store::loadRouteFile(path, false);
  } else if (sourceType == LoadedNavSource::NavFile) {
    loaded = nav_readFile(SD_MMC, path);
  } else if (sourceType == LoadedNavSource::UserWaypoints) {
    loaded = user_waypoints::loadAsNavigatorSource(false);
  }
  if (!loaded) return false;

  JsonObjectConst active = doc["active"].as<JsonObjectConst>();
  const char* activeType = active["type"] | "none";
  const uint8_t index = active["index"] | 0;
  const int16_t routePointIndex = active["route_point_index"] | 1;
  if (strcmp(activeType, "route") == 0) {
    const RouteID routeIndex(index);
    const RouteIndex pointIndex(routePointIndex);
    if (!routeIndex || routeIndex > totalRoutes || pointIndex < 1 ||
        pointIndex > routes[routeIndex].totalPoints)
      return false;
    lastNavType_ = LastNavType::Route;
    lastRouteIndex_ = routeIndex;
    lastRoutePointIndex_ = pointIndex;
    lastWaypointIndex_ = WaypointID::None;
    return activate ? activateRoute(lastRouteIndex_, lastRoutePointIndex_, false) : true;
  }
  if (strcmp(activeType, "point") == 0) {
    const WaypointID pointIndex(index);
    if (!pointIndex || pointIndex > totalWaypoints) return false;
    lastNavType_ = LastNavType::Point;
    lastRouteIndex_ = RouteID::None;
    lastRoutePointIndex_ = RouteIndex::None;
    lastWaypointIndex_ = pointIndex;
    return activate ? activatePoint(lastWaypointIndex_, false) : true;
  }

  lastNavType_ = LastNavType::None;
  lastRouteIndex_ = RouteID::None;
  lastRoutePointIndex_ = RouteIndex::None;
  lastWaypointIndex_ = WaypointID::None;
  savePersistedState();
  return true;
}

bool Navigator::resumeLastNav() {
  if (!hasLastNav() && !loadPersistedState(false)) return false;
  if (lastNavType_ == LastNavType::Route)
    return activateRoute(lastRouteIndex_,
                         lastRoutePointIndex_ ? lastRoutePointIndex_ : RouteIndex(1));
  if (lastNavType_ == LastNavType::Point) return activatePoint(lastWaypointIndex_);
  return false;
}

bool Navigator::restartLastRoute() {
  if (!hasLastNav() && !loadPersistedState(false)) return false;
  if (lastNavType_ != LastNavType::Route) return false;
  return activateRoute(lastRouteIndex_, RouteIndex(1));
}

bool gpx_readFile(fs::FS& fs, String fileName) {
  FileReader file_reader(fs, fileName);
  if (file_reader.error() != "") {
    Serial.print("Found file_reader error: ");
    Serial.println(file_reader.error());
    return false;
  }

  navigator.clear();

  GPXParser parser(&file_reader);
  bool success = parser.parse(&navigator);
  if (success) {
    Serial.println("gpx_readFile was successful:");
    Serial.print("  ");
    Serial.print(navigator.totalWaypoints);
    Serial.println(" waypoints");
    for (uint8_t wp = 1; wp <= navigator.totalWaypoints; wp++) {
      Serial.print("    ");
      Serial.print(navigator.waypoint(WaypointID(wp)).name);
      Serial.print(" @ ");
      Serial.print(navigator.waypoint(WaypointID(wp)).latitude());
      Serial.print(", ");
      Serial.print(navigator.waypoint(WaypointID(wp)).longitude());
      Serial.print(", ");
      Serial.print(navigator.waypoint(WaypointID(wp)).ele);
      Serial.println("m");
    }
    Serial.print("  ");
    Serial.print(navigator.totalRoutes);
    Serial.println(" routes");
    for (uint8_t r = 1; r <= navigator.totalRoutes; r++) {
      Serial.print("    ");
      Serial.print(navigator.routes[r].name);
      Serial.print(" (");
      Serial.print(navigator.routes[r].totalPoints);
      Serial.println(" points)");
      for (uint8_t wp = 1; wp <= navigator.routes[r].totalPoints; wp++) {
        Serial.print("      ");
        Serial.print(navigator.routePoint(RouteID(r), RouteIndex(wp)).name);
        Serial.print(" @ ");
        Serial.print(navigator.routePoint(RouteID(r), RouteIndex(wp)).latitude());
        Serial.print(", ");
        Serial.print(navigator.routePoint(RouteID(r), RouteIndex(wp)).longitude());
        Serial.print(", ");
        Serial.print(navigator.routePoint(RouteID(r), RouteIndex(wp)).ele);
        Serial.println("m");
      }
    }
    Serial.print("Navigator loaded ");
    Serial.print(navigator.totalWaypoints);
    Serial.print(" waypoints and ");
    Serial.print(navigator.totalRoutes);
    Serial.println(" routes");
    navigator.setLoadedGpxFilename(fileName);
    navigator.savePersistedState();
    return true;
  } else {
    // TODO: Display error to user (create appropriate method in GPXParser looking at _error, _line,
    // and _col)
    Serial.print("gpx_readFile error parsing GPX at line ");
    Serial.print(parser.line());
    Serial.print(" col ");
    Serial.print(parser.col());
    Serial.print(": ");
    Serial.println(parser.error());
    navigator.clear();
    return false;
  }
}

bool nav_readFile(fs::FS& fs, String fileName) {
  const String ext = lowerExtension(fileName);
  if (ext == "gpx") {
    return gpx_readFile(fs, fileName);
  }

  navigator.clear();

  bool success = false;
  if (ext == "cup") {
    success = parseCupFile(fs, fileName, &navigator);
  } else if (ext == "wpt" || ext == "wyp") {
    success = parseWptFile(fs, fileName, &navigator);
  } else if (ext == "kml") {
    success = parseKmlFile(fs, fileName, &navigator);
  } else {
    Serial.print("Unsupported nav file type: ");
    Serial.println(fileName);
    return false;
  }

  if (success) {
    Serial.print("Navigator loaded ");
    Serial.print(navigator.totalWaypoints);
    Serial.print(" waypoints and ");
    Serial.print(navigator.totalRoutes);
    Serial.print(" routes from ");
    Serial.println(fileName);
    navigator.setLoadedNavFilename(fileName);
    navigator.savePersistedState();
    return true;
  }

  Serial.print("nav_readFile error parsing ");
  Serial.println(fileName);
  navigator.clear();
  return false;
}

void Navigator::loadRoutes() {
  totalRoutes = 4;

  routes[1].setName("R: TheCircuit");
  addRoutePoint(&routes[1], WaypointID(1));
  addRoutePoint(&routes[1], WaypointID(7));
  addRoutePoint(&routes[1], WaypointID(8));
  addRoutePoint(&routes[1], WaypointID(1));
  addRoutePoint(&routes[1], WaypointID(2));

  routes[2].setName("R: Scenic");
  addRoutePoint(&routes[2], WaypointID(1));
  addRoutePoint(&routes[2], WaypointID(3));
  addRoutePoint(&routes[2], WaypointID(4));
  addRoutePoint(&routes[2], WaypointID(5));
  addRoutePoint(&routes[2], WaypointID(2));

  routes[3].setName("R: Downhill");
  addRoutePoint(&routes[3], WaypointID(1));
  addRoutePoint(&routes[3], WaypointID(5));
  addRoutePoint(&routes[3], WaypointID(2));

  routes[4].setName("R: MiniTri");
  addRoutePoint(&routes[4], WaypointID(1));
  addRoutePoint(&routes[4], WaypointID(4));
  addRoutePoint(&routes[4], WaypointID(5));
  addRoutePoint(&routes[4], WaypointID(1));
}

void Navigator::loadWaypoints() {
  clear();

  Waypoint waypoint;
  waypoint.setName("Marshall");
  waypoint.setCoordinates(34.21016, -117.30274);
  waypoint.ele = 1223;
  addWaypoint(waypoint);
  waypoint.setName("Marshall_LZ");
  waypoint.setCoordinates(34.19318, -117.32334);
  waypoint.ele = 521;
  addWaypoint(waypoint);
  waypoint.setName("Cloud");
  waypoint.setCoordinates(34.21065, -117.31298);
  waypoint.ele = 1169;
  addWaypoint(waypoint);
  waypoint.setName("Regionals");
  waypoint.setCoordinates(34.20958, -117.31982);
  waypoint.ele = 1033;
  addWaypoint(waypoint);
  waypoint.setName("750");
  waypoint.setCoordinates(34.19991, -117.31688);
  waypoint.ele = 767;
  addWaypoint(waypoint);
  waypoint.setName("Crestline");
  waypoint.setCoordinates(34.23604, -117.31321);
  waypoint.ele = 1611;
  addWaypoint(waypoint);
  waypoint.setName("Billboard");
  waypoint.setCoordinates(34.23531, -117.32608);
  waypoint.ele = 1572;
  addWaypoint(waypoint);
  waypoint.setName("Pine_Mt");
  waypoint.setCoordinates(34.23762, -117.35115);
  waypoint.ele = 1553;
  addWaypoint(waypoint);
}
