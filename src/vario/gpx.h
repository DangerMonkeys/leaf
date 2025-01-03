#ifndef gpx_h
#define gpx_h

#include <Arduino.h>
#include <FS.h>

#define AVERAGE_SPEED_SAMPLES 5

// Waypoint definition and memory allocation
#define waypointRadius 150  // meters radius to count as "reaching/crossing" a waypoint
#define maxWaypoints 255
#define maxRoutes 20
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

// GPXdata object for storing available waypoints and routes
struct GPXdata {
  Waypoint waypoints[maxWaypoints];
  uint8_t totalWaypoints = 0;
  Route routes[maxRoutes];
  uint8_t totalRoutes = 0;
};
extern GPXdata gpxData;

// GPXnav object for storing current nav info (used largely for display purposes)
struct GPXnav {
  Waypoint activePoint;  // waypoint currently navigating to
  Waypoint nextPoint;    // next waypoint in the current route
  Waypoint goalPoint;    // final waypoint in the current route

  int16_t activePointIndex =
      0;  // waypoint currently navigating to (index value for element inside of waypoints[], or
          // inside of route.routepoints[], if on an active route)
  int16_t nextPointIndex =
      0;  // the next waypoint (can prepare you which direction you'll need to turn next as you
          // approach the currently active waypoint).  We create this as a separate variable
          // (instead of just adding 1 to the acive index) because sometimes there IS NO next point
          // (i.e., you're on the last point) and we want to know this.
  int16_t activeRouteIndex =
      0;  // route currently navigating along (index value for route inside of routes[])

  int32_t altAboveWaypoint = 0;  // (gps measured) Altitude in cm above current waypoint
  int32_t altAboveGoal = 0;      // (gps measured) Altitude in cm above goal waypoint

  float averageSpeed =
      0;  // keep a running average speed, to smooth out glide ratio and time-remaning calculations.

  float glideToActive = 0;  // glide ratio from current position to active waypoint
  float glideToGoal = 0;    // glide ratio from current position to final (goal) waypoint, ALONG the
                            // route //TODO: should this be along route or straight to?

  double segmentDistance;         // distance between adjacent waypoints
  double pointDistanceRemaining;  // distance remaining to next waypoint
  uint32_t pointTimeRemaining;    // time (seconds) remaning to next waypoint
  double totalDistanceRemaining;  // distance remaining to last waypoint
  uint32_t totalTimeRemaining;    // time (seconds) remaning to final waypoint
  double courseToActive;          // heading degrees from current location to active waypoint
  double courseToNext;  // heading degrees from current location to next waypoint (the one after
                        // active)
  double turnToActive;  // change-in-current-heading to point toward active point
  double turnToNext;    // change-in-current-heading to point toward next point

  bool navigating = false;   // are we currently navigating to any destination
  bool reachedGoal = false;  // when finished with the Route, we might want to stay in a "finished"
                             // state instead of cancelling navigation altogether
};
extern GPXnav gpxNav;

void gpx_initNav(void);
void updateGPXnav(void);
bool gpx_activatePoint(int16_t pointIndex);
bool gpx_activateRoute(uint16_t routeIndex);
bool gpx_sequenceWaypoint(void);
void gpx_cancelNav(void);

bool gpx_readFile(fs::FS &fs, String fileName);

void gpx_loadRoutes(void);
void gpx_loadWaypoints(void);

#endif