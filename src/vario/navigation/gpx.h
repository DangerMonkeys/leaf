#pragma once

#include <Arduino.h>
#include <FS.h>
#include <string.h>

#include "navigation/nav_ids.h"

#define AVERAGE_SPEED_SAMPLES 5

// Waypoint definition and memory allocation
#define defaultWaypointRadius 150  // meters radius to count as "reaching/crossing" a waypoint
#define maxRoutes 10
#define maxNavPoints 120
#define maxRoutePointRefs 40
#define maxGpxNameLength 15
#define maxGpxFileNameLength 96

inline int32_t gpxDegreesToE7(double degrees) {
  return static_cast<int32_t>(degrees * 10000000.0 + (degrees >= 0 ? 0.5 : -0.5));
}

inline double gpxE7ToDegrees(int32_t degreesE7) { return degreesE7 / 10000000.0; }

struct Waypoint {
  char name[maxGpxNameLength + 1] = "";
  int32_t latE7 = 0;
  int32_t lonE7 = 0;
  float ele = 0;

  void setName(const char* value) {
    strncpy(name, value ? value : "", maxGpxNameLength);
    name[maxGpxNameLength] = '\0';
  }

  void setLatitude(double latitude) { latE7 = gpxDegreesToE7(latitude); }
  void setLongitude(double longitude) { lonE7 = gpxDegreesToE7(longitude); }
  void setCoordinates(double latitude, double longitude) {
    setLatitude(latitude);
    setLongitude(longitude);
  }
  double latitude() const { return gpxE7ToDegrees(latE7); }
  double longitude() const { return gpxE7ToDegrees(lonE7); }
};

enum class RoutePointRole : uint8_t {
  Normal,
  Takeoff,
  StartSpeedSection,
  EndSpeedSection,
  EndSpeedSectionGoal,
  Goal,
};

enum class RouteTaskType : uint8_t {
  Route,
  Classic,
};

enum class RouteEarthModel : uint8_t {
  WGS84,
  FAISphere,
};

enum class RouteGoalType : uint8_t {
  Cylinder,
  Line,
};

enum class RouteStartType : uint8_t {
  None,
  Race,
  ElapsedTime,
};

enum class LoadedNavSource : uint8_t {
  None,
  NavFile,
  SavedRoute,
  UserWaypoints,
};

enum class LastNavType : uint8_t {
  None,
  Route,
  Point,
};

struct RoutePoint {
  WaypointID waypointIndex = WaypointID::None;
  uint16_t radiusM = defaultWaypointRadius;
  RoutePointRole role = RoutePointRole::Normal;
};

// Route definition and memory allocation
struct Route {
  char name[maxGpxNameLength + 1] = "";
  uint8_t firstRoutePointIndex = 0;
  uint8_t totalPoints = 0;
  RouteTaskType taskType = RouteTaskType::Route;
  RouteEarthModel earthModel = RouteEarthModel::WGS84;
  RouteGoalType goalType = RouteGoalType::Cylinder;
  RouteStartType startType = RouteStartType::None;
  uint16_t goalDeadlineMinutesUtc = 0;
  bool hasGoalDeadline = false;

  void setName(const char* value) {
    strncpy(name, value ? value : "", maxGpxNameLength);
    name[maxGpxNameLength] = '\0';
  }
};

// Navigator class for managing nav info (used largely for display purposes)
class Navigator {
 public:
  void init(void);
  void update(void);

  bool activatePoint(WaypointID pointIndex);
  bool activatePoint(WaypointID pointIndex, bool playSound);
  bool activateRoute(RouteID routeIndex);
  bool activateRoute(RouteID routeIndex, bool playSound);
  bool activateRoute(RouteID routeIndex, RouteIndex routePointIndex);
  bool activateRoute(RouteID routeIndex, RouteIndex routePointIndex, bool playSound);
  void cancelNav(void);
  bool loadPersistedState();
  bool loadPersistedState(bool activate);
  bool savePersistedState();
  bool clearPersistedState();
  bool resumeLastNav();
  bool restartLastRoute();

  // True if a specific waypoint is active, or if a route is active with a next route point.
  bool hasActivePoint() const;
  bool hasNavSolution() const;
  bool hasLastNav() const { return lastNavType_ != LastNavType::None; }
  bool lastNavIsRoute() const { return lastNavType_ == LastNavType::Route; }
  bool lastNavIsPoint() const { return lastNavType_ == LastNavType::Point; }
  RouteID lastRouteIndex() const { return lastRouteIndex_; }
  RouteIndex lastRoutePointIndex() const { return lastRoutePointIndex_; }
  RouteID routeContextIndex() const;
  const char* lastNavDestinationName() const;

  void clear();
  bool addWaypoint(const Waypoint& waypoint);
  WaypointID addOrFindWaypoint(const Waypoint& waypoint);
  WaypointID findWaypointByName(const char* name) const;
  bool addRoutePoint(Route* route, WaypointID waypointIndex,
                     uint16_t radiusM = defaultWaypointRadius,
                     RoutePointRole role = RoutePointRole::Normal);
  const Waypoint& waypoint(WaypointID pointIndex) const;
  const Waypoint& routePoint(RouteID routeIndex, RouteIndex pointIndex) const;
  const RoutePoint& routePointMeta(RouteID routeIndex, RouteIndex pointIndex) const;
  bool hasLoadedGpxFile() const { return loadedNavSource_ != LoadedNavSource::None; }
  bool hasLoadedNavFile() const { return loadedNavSource_ == LoadedNavSource::NavFile; }
  bool hasLoadedSavedRoute() const { return loadedNavSource_ == LoadedNavSource::SavedRoute; }
  bool hasLoadedUserWaypoints() const { return loadedNavSource_ == LoadedNavSource::UserWaypoints; }
  uint8_t loadedFileWaypointCount() const { return loadedFileWaypointCount_; }
  void markLoadedFileWaypointCount() { loadedFileWaypointCount_ = totalWaypoints; }
  const char* loadedGpxFilename() const { return loadedGpxFilename_; }
  const char* loadedNavFilename() const { return loadedGpxFilename(); }
  const char* loadedNavPath() const { return loadedNavPath_; }
  void setLoadedGpxFilename(const String& fileName);
  void setLoadedNavFilename(const String& fileName) { setLoadedGpxFilename(fileName); }
  void setLoadedSavedRouteFilename(const String& fileName);
  void setLoadedUserWaypointsFilename(const String& fileName);

  Waypoint waypoints[maxNavPoints + 1];
  uint8_t totalWaypoints = 0;
  RoutePoint routePoints[maxRoutePointRefs + 1];
  uint8_t totalRoutePointRefs = 0;
  Route routes[maxRoutes + 1];
  uint8_t totalRoutes = 0;

  // waypoint currently navigating to
  Waypoint activePoint;
  RoutePoint activeRoutePoint;

  // waypoint currently navigating to (display index inside the bare waypoint list)
  WaypointID activeWaypointIndex;
  // route point currently navigating to (one-based index inside the active route)
  RouteIndex activeRoutePointIndex;
  // route currently navigating along (index value for route inside of routes[])
  RouteID activeRouteIndex;

  // (gps measured) Altitude in cm above current waypoint
  int32_t altAboveWaypoint = 0;

  // keep a running average speed, to smooth out glide ratio and time-remaning calculations.
  float averageSpeed = 0;

  // glide ratio from current position to active waypoint
  float glideToActive = 0;

  // distance between adjacent waypoints
  double segmentDistance;
  // distance remaining to next waypoint
  double pointDistanceRemaining;
  // time (seconds) remaning to next waypoint
  uint32_t pointTimeRemaining;
  // change-in-current-heading to point toward active point
  double turnToActive;

  // are we currently navigating to any destination
  bool navigating = false;

 private:
  bool gpsPositionUsable() const;
  void clearNavSolution();
  bool sequenceWaypoint(bool playSound = true);
  void loadRoutes(void);
  void loadWaypoints(void);

  // next waypoint in the current route
  Waypoint nextPoint_;
  // final waypoint in the current route
  Waypoint goalPoint_;

  // the next waypoint (can prepare you which direction you'll need to turn next as you
  // approach the currently active waypoint).  We create this as a separate variable
  // (instead of just adding 1 to the acive index) because sometimes there IS NO next point
  // (i.e., you're on the last point) and we want to know this.
  RouteIndex nextPointIndex_;

  // (gps measured) Altitude in cm above goal waypoint
  int32_t altAboveGoal_ = 0;

  // glide ratio from current position to final (goal) waypoint, ALONG the
  // route //TODO: should this be along route or straight to?
  float glideToGoal_ = 0;

  // distance remaining to last waypoint
  double totalDistanceRemaining_;

  // heading degrees from current location to active waypoint
  double courseToActive_;

  // heading degrees from current location to next waypoint (the one after active)
  double courseToNext_;

  // change-in-current-heading to point toward next point
  double turnToNext_;

  // when finished with the Route, we might want to stay in a "finished"
  // state instead of cancelling navigation altogether
  bool reachedGoal_ = false;
  uint8_t loadedFileWaypointCount_ = 0;
  LoadedNavSource loadedNavSource_ = LoadedNavSource::None;
  char loadedGpxFilename_[maxGpxFileNameLength + 1] = "";
  char loadedNavPath_[maxGpxFileNameLength + 1] = "";
  LastNavType lastNavType_ = LastNavType::None;
  RouteID lastRouteIndex_ = RouteID::None;
  RouteIndex lastRoutePointIndex_ = RouteIndex::None;
  WaypointID lastWaypointIndex_ = WaypointID::None;
};
extern Navigator navigator;

bool gpx_readFile(fs::FS& fs, String fileName);
bool nav_readFile(fs::FS& fs, String fileName);
