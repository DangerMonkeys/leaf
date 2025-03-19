

// GPX is the common gps file format for storing waypoints, routes, and tracks, etc.
// This CPP file is for functions related to navigating and tracking active waypoints and routes
// (Though Leaf may/will support other file types in the future, we'll still use this gpx.cpp file
// even when dealing with other datatypes)

#include "gpx.h"

#include <FS.h>
#include <SD_MMC.h>

#include "baro.h"
#include "files.h"
#include "gps.h"
#include "gpx_parser.h"
#include "speaker.h"

Waypoint waypoints[maxWaypoints];
Route routes[maxRoutes];
Navigator navigator;

// TODO: at least here for testing so we can be navigating right from boot up
void Navigator::init() {
  Serial.println("Loading GPX file...");
  delay(100);
  bool result = gpx_readFile(SD_MMC, "/waypoints.gpx");
  Serial.print("gpx_readFile result: ");
  Serial.println(result ? "true" : "false");

  // loadWaypoints();
  // loadRoutes();
  // activatePoint(19);
  // activateRoute(3);
}

// update nav data every second
void Navigator::update() {
  // only update nav info if we're tracking to an active point
  if (hasActivePoint()) {
    // update distance remaining, then sequence to next point if distance is small enough
    pointDistanceRemaining = gps.distanceBetween(gps.location.lat(), gps.location.lng(),
                                                 activePoint.lat, activePoint.lon);
    if (pointDistanceRemaining < waypointRadius && !reachedGoal_)
      sequenceWaypoint();  //  (this will also update distance to the new point)

    // update time remaining
    if (gps.speed.mps() < 0.5) {
      pointTimeRemaining = 0;
    } else {
      pointTimeRemaining = (pointDistanceRemaining - waypointRadius) / gps.speed.mps();
    }

    // get degress to active point
    courseToActive_ =
        gps.courseTo(gps.location.lat(), gps.location.lng(), activePoint.lat, activePoint.lon);
    turnToActive = courseToActive_ - gps.course.deg();
    if (turnToActive > 180)
      turnToActive -= 360;
    else if (turnToActive < -180)
      turnToActive += 360;

    // if there's a next point, get course to that as well
    if (nextPointIndex_) {
      courseToNext_ =
          gps.courseTo(gps.location.lat(), gps.location.lng(), nextPoint_.lat, nextPoint_.lon);
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
  averageSpeed =
      (averageSpeed * (AVERAGE_SPEED_SAMPLES - 1) + gps.speed.kmph()) / AVERAGE_SPEED_SAMPLES;
}

// Start, Sequence, and End Navigation Functions

bool Navigator::activatePoint(WaypointID pointIndex) {
  navigating = true;
  reachedGoal_ = false;

  // Point navigation is exclusive from Route navigation, so cancel any Route navigation
  activeRouteIndex = RouteID::None;
  activeRoutePointIndex = RouteIndex::None;

  activeWaypointIndex = pointIndex;
  activePoint = waypoints[activeWaypointIndex];

  speaker_playSound(fx_enter);

  double newDistance =
      gps.distanceBetween(gps.location.lat(), gps.location.lng(), activePoint.lat, activePoint.lon);

  segmentDistance = newDistance;
  totalDistanceRemaining_ = newDistance;
  pointDistanceRemaining = newDistance;

  return navigating;
}

bool Navigator::activateRoute(RouteID routeIndex) {
  // first check if any valid points
  uint8_t validPoints = routes[routeIndex].totalPoints;
  if (!validPoints) {
    navigating = false;
  } else {
    navigating = true;
    reachedGoal_ = false;
    activeRouteIndex = routeIndex;

    Serial.print("*** NEW ROUTE: ");
    Serial.println(routes[activeRouteIndex].name);

    // set activePointIndex to 0, then call sequenceWaypoint() to increment and populate new
    // activePoint, and nextPoint, if any
    activeRoutePointIndex = RouteIndex::None;
    sequenceWaypoint();

    // calculate TOTAL Route distance
    totalDistanceRemaining_ = 0;
    // if we have at least 2 points:
    if (routes[activeRouteIndex].totalPoints >= 2) {
      for (int i = 1; i < routes[activeRouteIndex].totalPoints; i++) {
        totalDistanceRemaining_ +=
            gps.distanceBetween(routes[activeRouteIndex].routepoints[i].lat,
                                routes[activeRouteIndex].routepoints[i].lon,
                                routes[activeRouteIndex].routepoints[i + 1].lat,
                                routes[activeRouteIndex].routepoints[i + 1].lon);
      }
      // otherwise our Route only has 1 point, so the Route distance is from where we are now to
      // that one point
    } else if (routes[activeRouteIndex].totalPoints == 1) {
      totalDistanceRemaining_ = gps.distanceBetween(gps.location.lat(), gps.location.lng(),
                                                    routes[activeRouteIndex].routepoints[1].lat,
                                                    routes[activeRouteIndex].routepoints[1].lon);
    }
  }
  return navigating;
}

bool Navigator::sequenceWaypoint() {
  Serial.print("entering sequence..");

  bool successfulSequence = false;

  // sequence to next point if we're on a route && there's another point in the Route
  if (activeRouteIndex && activeRoutePointIndex < routes[activeRouteIndex].totalPoints) {
    successfulSequence = true;

    // TODO: play going to next point sound, or whatever
    speaker_playSound(fx_enter);

    activeRoutePointIndex++;
    Serial.print(" new active index:");
    Serial.print(activeRoutePointIndex);
    Serial.print(" route index:");
    Serial.print(activeRouteIndex);
    activePoint = routes[activeRouteIndex].routepoints[activeRoutePointIndex];

    Serial.print(" new point:");
    Serial.print(activePoint.name);
    Serial.print(" new lat: ");
    Serial.print(activePoint.lat);

    if (activeRoutePointIndex + 1 <
        routes[activeRouteIndex]
            .totalPoints) {  // if there's also a next point in the list, capture that
      nextPointIndex_ = activeRoutePointIndex + 1;
      nextPoint_ = routes[activeRouteIndex].routepoints[nextPointIndex_];
    } else {  // otherwise signify no next point, so we don't show display functions related to next
              // point
      nextPointIndex_ = RouteIndex::NoNextPoint;
    }

    // get distance between present (prev) point and new activePoint (used for distance progress
    // bar) if we're sequencing to the very first point, then there's no previous point to use, so
    // use our current location instead
    if (activeRoutePointIndex == 1) {
      segmentDistance =
          gps.distanceBetween(gps.location.lat(), gps.location.lng(),
                              routes[activeRouteIndex].routepoints[activeRoutePointIndex].lat,
                              routes[activeRouteIndex].routepoints[activeRoutePointIndex].lon);
    } else {
      segmentDistance =
          gps.distanceBetween(routes[activeRouteIndex].routepoints[activeRoutePointIndex - 1].lat,
                              routes[activeRouteIndex].routepoints[activeRoutePointIndex - 1].lon,
                              routes[activeRouteIndex].routepoints[activeRoutePointIndex].lat,
                              routes[activeRouteIndex].routepoints[activeRoutePointIndex].lon);
    }

  } else {  // otherwise, we made it to our destination!
    // TODO: celebrate!  (play reaching goal sound, or whatever)
    reachedGoal_ = true;
    speaker_playSound(fx_confirm);
  }
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
  reachedGoal_ = false;
  navigating = false;
  turnToActive = 0;
  turnToNext_ = 0;
  speaker_playSound(fx_cancel);
}

bool Navigator::hasActivePoint() { return activeWaypointIndex || activeRoutePointIndex; }

Navigator parse_result;

bool gpx_readFile(fs::FS& fs, String fileName) {
  FileReader file_reader(fs, fileName);
  if (file_reader.error() != "") {
    Serial.print("Found file_reader error: ");
    Serial.println(file_reader.error());
    return false;
  }
  GPXParser parser(&file_reader);
  bool success = parser.parse(&parse_result);
  if (success) {
    Serial.println("gpx_readFile was successful:");
    Serial.print("  ");
    Serial.print(parse_result.totalWaypoints);
    Serial.println(" waypoints");
    for (uint8_t wp = 0; wp < parse_result.totalWaypoints; wp++) {
      Serial.print("    ");
      Serial.print(parse_result.waypoints[wp].name);
      Serial.print(" @ ");
      Serial.print(parse_result.waypoints[wp].lat);
      Serial.print(", ");
      Serial.print(parse_result.waypoints[wp].lon);
      Serial.print(", ");
      Serial.print(parse_result.waypoints[wp].ele);
      Serial.println("m");
    }
    Serial.print("  ");
    Serial.print(parse_result.totalRoutes);
    Serial.println(" routes");
    for (uint8_t r = 0; r < parse_result.totalRoutes; r++) {
      Serial.print("    ");
      Serial.print(parse_result.routes[r].name);
      Serial.print(" (");
      Serial.print(parse_result.routes[r].totalPoints);
      Serial.println(" points)");
      for (uint8_t wp = 0; wp < parse_result.routes[r].totalPoints; wp++) {
        Serial.print("      ");
        Serial.print(parse_result.waypoints[wp].name);
        Serial.print(" @ ");
        Serial.print(parse_result.waypoints[wp].lat);
        Serial.print(", ");
        Serial.print(parse_result.waypoints[wp].lon);
        Serial.print(", ");
        Serial.print(parse_result.waypoints[wp].ele);
        Serial.println("m");
      }
    }
    navigator = parse_result;
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
    return false;
  }
}

void Navigator::loadRoutes() {
  routes[1].name = "R: TheCircuit";
  routes[1].totalPoints = 5;
  routes[1].routepoints[1] = waypoints[1];
  routes[1].routepoints[2] = waypoints[7];
  routes[1].routepoints[3] = waypoints[8];
  routes[1].routepoints[4] = waypoints[1];
  routes[1].routepoints[5] = waypoints[2];

  routes[2].name = "R: Scenic";
  routes[2].totalPoints = 5;
  routes[2].routepoints[1] = waypoints[1];
  routes[2].routepoints[2] = waypoints[3];
  routes[2].routepoints[3] = waypoints[4];
  routes[2].routepoints[4] = waypoints[5];
  routes[2].routepoints[5] = waypoints[2];

  routes[3].name = "R: Downhill";
  routes[3].totalPoints = 4;
  routes[3].routepoints[1] = waypoints[1];
  routes[3].routepoints[2] = waypoints[5];
  routes[3].routepoints[3] = waypoints[2];

  routes[4].name = "R: MiniTri";
  routes[4].totalPoints = 4;
  routes[4].routepoints[1] = waypoints[1];
  routes[4].routepoints[2] = waypoints[4];
  routes[4].routepoints[3] = waypoints[5];
  routes[4].routepoints[4] = waypoints[1];

  totalRoutes = 4;
}

void Navigator::loadWaypoints() {
  waypoints[0].lat = 0;
  waypoints[0].lon = 0;
  waypoints[0].ele = 0;
  waypoints[0].name = "EMPTY_POINT";

  waypoints[1].lat = 34.21016;
  waypoints[1].lon = -117.30274;
  waypoints[1].ele = 1223;
  waypoints[1].name = "Marshall";

  waypoints[2].lat = 34.19318;
  waypoints[2].lon = -117.32334;
  waypoints[2].ele = 521;
  waypoints[2].name = "Marshall_LZ";

  waypoints[3].lat = 34.21065;
  waypoints[3].lon = -117.31298;
  waypoints[3].ele = 1169;
  waypoints[3].name = "Cloud";

  waypoints[4].lat = 34.20958;
  waypoints[4].lon = -117.31982;
  waypoints[4].ele = 1033;
  waypoints[4].name = "Regionals";

  waypoints[5].lat = 34.19991;
  waypoints[5].lon = -117.31688;
  waypoints[5].ele = 767;
  waypoints[5].name = "750";

  waypoints[6].lat = 34.23604;
  waypoints[6].lon = -117.31321;
  waypoints[6].ele = 1611;
  waypoints[6].name = "Crestline";

  waypoints[7].lat = 34.23531;
  waypoints[7].lon = -117.32608;
  waypoints[7].ele = 1572;
  waypoints[7].name = "Billboard";

  waypoints[8].lat = 34.23762;
  waypoints[8].lon = -117.35115;
  waypoints[8].ele = 1553;
  waypoints[8].name = "Pine_Mt";

  totalWaypoints = 8;
}
