#pragma once

#include <Arduino.h>
#include <FS.h>

#define AVERAGE_SPEED_SAMPLES 5

// Waypoint definition and memory allocation
#define waypointRadius 150  // meters radius to count as "reaching/crossing" a waypoint
#define maxWaypoints 15
#define maxRoutes 5
#define maxRoutePoints 12

struct Waypoint {
  String name;
  double lat;
  double lon;
  float ele;
};

// Route definition and memory allocation
struct Route {
  String name;
  uint8_t totalPoints = 0;
  Waypoint routepoints[maxRoutePoints];
};

// Navigator class for managing nav info (used largely for display purposes)
class Navigator {
 public:
  void init(void);
  void update(void);

  bool activatePoint(int16_t pointIndex);
  bool activateRoute(uint16_t routeIndex);
  void cancelNav(void);

  Waypoint waypoints[maxWaypoints];
  uint8_t totalWaypoints = 0;
  Route routes[maxRoutes];
  uint8_t totalRoutes = 0;

  // waypoint currently navigating to
  Waypoint activePoint;

  // waypoint currently navigating to (index value for element inside of waypoints[], or
  // inside of route.routepoints[], if on an active route)
  int16_t activePointIndex = 0;
  // route currently navigating along (index value for route inside of routes[])
  int16_t activeRouteIndex = 0;

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
  bool sequenceWaypoint(void);
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
  int16_t nextPointIndex_ = 0;

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
};
extern Navigator navigator;

bool gpx_readFile(fs::FS& fs, String fileName);
