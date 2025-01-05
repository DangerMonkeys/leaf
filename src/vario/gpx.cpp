

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
GPXnav gpxNav = {};
GPXdata gpxData = {};

// TODO: at least here for testing so we can be navigating right from boot up
void gpx_initNav() {
  Serial.println("Loading GPX file...");
  delay(100);
  bool result = gpx_readFile(SD_MMC, "/waypoints.gpx");
  Serial.print("gpx_readFile result: ");
  Serial.println(result ? "true" : "false");

  // gpx_loadWaypoints();
  // gpx_loadRoutes();
  // gpx_activatePoint(19);
  // gpx_activateRoute(3);
}

// update nav data every second
void updateGPXnav() {
  // only update nav info if we're tracking to an active point
  if (gpxNav.activePointIndex) {
    // update distance remaining, then sequence to next point if distance is small enough
    gpxNav.pointDistanceRemaining = gps.distanceBetween(
        gps.location.lat(), gps.location.lng(), gpxNav.activePoint.lat, gpxNav.activePoint.lon);
    if (gpxNav.pointDistanceRemaining < waypointRadius && !gpxNav.reachedGoal)
      gpx_sequenceWaypoint();  //  (this will also update distance to the new point)

    // update time remaining
    if (gps.speed.mps() < 0.5) {
      gpxNav.pointTimeRemaining = 0;
    } else {
      gpxNav.pointTimeRemaining =
          (gpxNav.pointDistanceRemaining - waypointRadius) / gps.speed.mps();
    }

    // get degress to active point
    gpxNav.courseToActive = gps.courseTo(
        gps.location.lat(), gps.location.lng(), gpxNav.activePoint.lat, gpxNav.activePoint.lon);
    gpxNav.turnToActive = gpxNav.courseToActive - gps.course.deg();
    if (gpxNav.turnToActive > 180)
      gpxNav.turnToActive -= 360;
    else if (gpxNav.turnToActive < -180)
      gpxNav.turnToActive += 360;

    // if there's a next point, get course to that as well
    if (gpxNav.nextPointIndex) {
      gpxNav.courseToNext = gps.courseTo(
          gps.location.lat(), gps.location.lng(), gpxNav.nextPoint.lat, gpxNav.nextPoint.lon);
      gpxNav.turnToNext = gpxNav.courseToNext - gps.course.deg();
      if (gpxNav.turnToNext > 180)
        gpxNav.turnToNext -= 360;
      else if (gpxNav.turnToNext < -180)
        gpxNav.turnToNext += 360;
    }

    // get glide to active (and goal point, if we're on a route)
    gpxNav.glideToActive =
        gpxNav.pointDistanceRemaining / (gps.altitude.meters() - gpxNav.activePoint.ele);
    if (gpxNav.activeRouteIndex)
      gpxNav.glideToGoal =
          gpxNav.totalDistanceRemaining / (gps.altitude.meters() - gpxNav.goalPoint.ele);

    // update relative altimeters (in cm)
    // alt above active point
    gpxNav.altAboveWaypoint = 100 * (gps.altitude.meters() - gpxNav.activePoint.ele);

    // alt above goal (if we're on a route and have a goal; otherwise, set relative goal alt to same
    // as next point)
    if (gpxNav.activeRouteIndex)
      gpxNav.altAboveGoal = 100 * (gps.altitude.meters() - gpxNav.goalPoint.ele);
    else
      gpxNav.altAboveGoal = gpxNav.altAboveWaypoint;
  }

  // update additional values that are required regardless of if we're navigating to a point
  // average speed
  gpxNav.averageSpeed = (gpxNav.averageSpeed * (AVERAGE_SPEED_SAMPLES - 1) + gps.speed.kmph()) /
                        AVERAGE_SPEED_SAMPLES;
}

// Start, Sequence, and End Navigation Functions

bool gpx_activatePoint(int16_t pointIndex) {
  gpxNav.navigating = true;
  gpxNav.reachedGoal = false;

  gpxNav.activeRouteIndex =
      0;  // Point navigation is exclusive from Route navigation, so cancel any Route navigation
  gpxNav.activePointIndex = pointIndex;
  gpxNav.activePoint = gpxData.waypoints[gpxNav.activePointIndex];

  speaker_playSound(fx_enter);

  double newDistance = gps.distanceBetween(
      gps.location.lat(), gps.location.lng(), gpxNav.activePoint.lat, gpxNav.activePoint.lon);

  gpxNav.segmentDistance = newDistance;
  gpxNav.totalDistanceRemaining = newDistance;
  gpxNav.pointDistanceRemaining = newDistance;

  return gpxNav.navigating;
}

bool gpx_activateRoute(uint16_t routeIndex) {
  // first check if any valid points
  uint8_t validPoints = gpxData.routes[routeIndex].totalPoints;
  if (!validPoints) {
    gpxNav.navigating = false;
  } else {
    gpxNav.navigating = true;
    gpxNav.reachedGoal = false;
    gpxNav.activeRouteIndex = routeIndex;

    Serial.print("*** NEW ROUTE: ");
    Serial.println(gpxData.routes[gpxNav.activeRouteIndex].name);

    // set activePointIndex to 0, then call sequenceWaypoint() to increment and populate new
    // activePoint, and nextPoint, if any
    gpxNav.activePointIndex = 0;
    gpx_sequenceWaypoint();

    // calculate TOTAL Route distance
    gpxNav.totalDistanceRemaining = 0;
    // if we have at least 2 points:
    if (gpxData.routes[gpxNav.activeRouteIndex].totalPoints >= 2) {
      for (int i = 1; i < routes[gpxNav.activeRouteIndex].totalPoints; i++) {
        gpxNav.totalDistanceRemaining +=
            gps.distanceBetween(gpxData.routes[gpxNav.activeRouteIndex].routepoints[i].lat,
                                gpxData.routes[gpxNav.activeRouteIndex].routepoints[i].lon,
                                gpxData.routes[gpxNav.activeRouteIndex].routepoints[i + 1].lat,
                                gpxData.routes[gpxNav.activeRouteIndex].routepoints[i + 1].lon);
      }
      // otherwise our Route only has 1 point, so the Route distance is from where we are now to
      // that one point
    } else if (gpxData.routes[gpxNav.activeRouteIndex].totalPoints == 1) {
      gpxNav.totalDistanceRemaining =
          gps.distanceBetween(gps.location.lat(),
                              gps.location.lng(),
                              gpxData.routes[gpxNav.activeRouteIndex].routepoints[1].lat,
                              gpxData.routes[gpxNav.activeRouteIndex].routepoints[1].lon);
    }
  }
  return gpxNav.navigating;
}

bool gpx_sequenceWaypoint() {
  Serial.print("entering sequence..");

  bool successfulSequence = false;

  // sequence to next point if we're on a route && there's another point in the Route
  if (gpxNav.activeRouteIndex &&
      gpxNav.activePointIndex < gpxData.routes[gpxNav.activeRouteIndex].totalPoints) {
    successfulSequence = true;

    // TODO: play going to next point sound, or whatever
    speaker_playSound(fx_enter);

    gpxNav.activePointIndex++;
    Serial.print(" new active index:");
    Serial.print(gpxNav.activePointIndex);
    Serial.print(" route index:");
    Serial.print(gpxNav.activeRouteIndex);
    gpxNav.activePoint =
        gpxData.routes[gpxNav.activeRouteIndex].routepoints[gpxNav.activePointIndex];

    Serial.print(" new point:");
    Serial.print(gpxNav.activePoint.name);
    Serial.print(" new lat: ");
    Serial.print(gpxNav.activePoint.lat);

    if (gpxNav.activePointIndex + 1 <
        gpxData.routes[gpxNav.activeRouteIndex]
            .totalPoints) {  // if there's also a next point in the list, capture that
      gpxNav.nextPointIndex = gpxNav.activePointIndex + 1;
      gpxNav.nextPoint = gpxData.routes[gpxNav.activeRouteIndex].routepoints[gpxNav.nextPointIndex];
    } else {  // otherwise signify no next point, so we don't show display functions related to next
              // point
      gpxNav.nextPointIndex = -1;
    }

    // get distance between present (prev) point and new activePoint (used for distance progress
    // bar) if we're sequencing to the very first point, then there's no previous point to use, so
    // use our current location instead
    if (gpxNav.activePointIndex == 1) {
      gpxNav.segmentDistance = gps.distanceBetween(
          gps.location.lat(),
          gps.location.lng(),
          gpxData.routes[gpxNav.activeRouteIndex].routepoints[gpxNav.activePointIndex].lat,
          gpxData.routes[gpxNav.activeRouteIndex].routepoints[gpxNav.activePointIndex].lon);
    } else {
      gpxNav.segmentDistance = gps.distanceBetween(
          gpxData.routes[gpxNav.activeRouteIndex].routepoints[gpxNav.activePointIndex - 1].lat,
          gpxData.routes[gpxNav.activeRouteIndex].routepoints[gpxNav.activePointIndex - 1].lon,
          gpxData.routes[gpxNav.activeRouteIndex].routepoints[gpxNav.activePointIndex].lat,
          gpxData.routes[gpxNav.activeRouteIndex].routepoints[gpxNav.activePointIndex].lon);
    }

  } else {  // otherwise, we made it to our destination!
    // TODO: celebrate!  (play reaching goal sound, or whatever)
    gpxNav.reachedGoal = true;
    speaker_playSound(fx_confirm);
  }
  Serial.print(" succes is: ");
  Serial.println(successfulSequence);
  return successfulSequence;
}

void gpx_cancelNav() {
  gpxNav.pointDistanceRemaining = 0;
  gpxNav.pointTimeRemaining = 0;
  gpxNav.activeRouteIndex = 0;
  gpxNav.activePointIndex = 0;
  gpxNav.reachedGoal = false;
  gpxNav.navigating = false;
  gpxNav.turnToActive = 0;
  gpxNav.turnToNext = 0;
  speaker_playSound(fx_cancel);
}

GPXdata parse_result;

bool gpx_readFile(fs::FS &fs, String fileName) {
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
    gpxData = parse_result;
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

void gpx_loadRoutes() {
  gpxData.routes[1].name = "R: TheCircuit";
  gpxData.routes[1].totalPoints = 5;
  gpxData.routes[1].routepoints[1] = gpxData.waypoints[1];
  gpxData.routes[1].routepoints[2] = gpxData.waypoints[7];
  gpxData.routes[1].routepoints[3] = gpxData.waypoints[8];
  gpxData.routes[1].routepoints[4] = gpxData.waypoints[1];
  gpxData.routes[1].routepoints[5] = gpxData.waypoints[2];

  gpxData.routes[2].name = "R: Scenic";
  gpxData.routes[2].totalPoints = 5;
  gpxData.routes[2].routepoints[1] = gpxData.waypoints[1];
  gpxData.routes[2].routepoints[2] = gpxData.waypoints[3];
  gpxData.routes[2].routepoints[3] = gpxData.waypoints[4];
  gpxData.routes[2].routepoints[4] = gpxData.waypoints[5];
  gpxData.routes[2].routepoints[5] = gpxData.waypoints[2];

  gpxData.routes[3].name = "R: Downhill";
  gpxData.routes[3].totalPoints = 4;
  gpxData.routes[3].routepoints[1] = gpxData.waypoints[1];
  gpxData.routes[3].routepoints[2] = gpxData.waypoints[5];
  gpxData.routes[3].routepoints[3] = gpxData.waypoints[2];

  gpxData.routes[4].name = "R: MiniTri";
  gpxData.routes[4].totalPoints = 4;
  gpxData.routes[4].routepoints[1] = gpxData.waypoints[1];
  gpxData.routes[4].routepoints[2] = gpxData.waypoints[4];
  gpxData.routes[4].routepoints[3] = gpxData.waypoints[5];
  gpxData.routes[4].routepoints[4] = gpxData.waypoints[1];

  gpxData.totalRoutes = 4;
}

void gpx_loadWaypoints() {
  gpxData.waypoints[0].lat = 0;
  gpxData.waypoints[0].lon = 0;
  gpxData.waypoints[0].ele = 0;
  gpxData.waypoints[0].name = "EMPTY_POINT";

  gpxData.waypoints[1].lat = 34.21016;
  gpxData.waypoints[1].lon = -117.30274;
  gpxData.waypoints[1].ele = 1223;
  gpxData.waypoints[1].name = "Marshall";

  gpxData.waypoints[2].lat = 34.19318;
  gpxData.waypoints[2].lon = -117.32334;
  gpxData.waypoints[2].ele = 521;
  gpxData.waypoints[2].name = "Marshall_LZ";

  gpxData.waypoints[3].lat = 34.21065;
  gpxData.waypoints[3].lon = -117.31298;
  gpxData.waypoints[3].ele = 1169;
  gpxData.waypoints[3].name = "Cloud";

  gpxData.waypoints[4].lat = 34.20958;
  gpxData.waypoints[4].lon = -117.31982;
  gpxData.waypoints[4].ele = 1033;
  gpxData.waypoints[4].name = "Regionals";

  gpxData.waypoints[5].lat = 34.19991;
  gpxData.waypoints[5].lon = -117.31688;
  gpxData.waypoints[5].ele = 767;
  gpxData.waypoints[5].name = "750";

  gpxData.waypoints[6].lat = 34.23604;
  gpxData.waypoints[6].lon = -117.31321;
  gpxData.waypoints[6].ele = 1611;
  gpxData.waypoints[6].name = "Crestline";

  gpxData.waypoints[7].lat = 34.23531;
  gpxData.waypoints[7].lon = -117.32608;
  gpxData.waypoints[7].ele = 1572;
  gpxData.waypoints[7].name = "Billboard";

  gpxData.waypoints[8].lat = 34.23762;
  gpxData.waypoints[8].lon = -117.35115;
  gpxData.waypoints[8].ele = 1553;
  gpxData.waypoints[8].name = "Pine_Mt";

  gpxData.totalWaypoints = 8;
}
