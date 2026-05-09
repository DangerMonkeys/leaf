#pragma once

#include "Arduino.h"
#include "instruments/baro.h"
#include "ui/settings/settings.h"

class FlightStats {
 public:
  int32_t alt = 0;  // baro alt in cm
  int32_t alt_start = 0;
  int32_t alt_end = 0;
  int32_t alt_max = 0;
  int32_t alt_min = 0;
  int32_t alt_above_launch = 0;
  int32_t alt_above_launch_max = 0;

  int32_t climb = 0;  // climb in cm/s
  int32_t climb_max = 0;
  int32_t climb_min = 0;

  float gpsalt = 0;  // alt in m, float
  float gpsalt_start = 0;
  float gpsalt_end = 0;
  float gpsalt_max = 0;
  float gpsalt_min = 0;
  float gpsalt_above_launch = 0;
  float gpsalt_above_launch_max = 0;

  float startLocationLat = 0;
  float startLocationLng = 0;
  float endLocationLat = 0;
  float endLocationLng = 0;

  float distanceStraightLine = 0;  // distance between start and end points, in m
  float distanceAlongPath = 0;     // accumulated distance (m) of actual flight path

  float speed = 0;      // speed in mps
  float speed_max = 0;  // max speed logged in mps

  float temperature = 0;
  float temperature_max = 0;
  float temperature_min = 0;

  float accel = 1;
  float accel_max = 1;
  float accel_min = 1;

  // Flight duration, in seconds
  unsigned long duration = 0;
  unsigned long logStartedAt = 0;  // Time flight started (seconds started since boot)

  String toString() const {
    String alt_units;
    if (settings.units_alt)
      alt_units = "(ft)";
    else
      alt_units = "(m)";

    String climb_units;
    if (settings.units_climb)
      climb_units = "(fpm)";
    else
      climb_units = "(m/s)";

    String temp_units;
    if (settings.units_temp)
      temp_units = "(F)";
    else
      temp_units = "(C)";

    // convert values to proper units before printing/saving
    auto alt_start_formatted = baro_altToUnits(alt_start, settings.units_alt);
    auto alt_max_formatted = baro_altToUnits(alt_max, settings.units_alt);
    auto alt_min_formatted = baro_altToUnits(alt_min, settings.units_alt);
    auto alt_end_formatted = baro_altToUnits(alt_end, settings.units_alt);
    auto alt_above_launch_max_formatted = baro_altToUnits(alt_above_launch_max, settings.units_alt);

    auto gpsalt_start_formatted = baro_altToUnits(int32_t(gpsalt_start * 100), settings.units_alt);
    auto gpsalt_max_formatted = baro_altToUnits(int32_t(gpsalt_max * 100), settings.units_alt);
    auto gpsalt_min_formatted = baro_altToUnits(int32_t(gpsalt_min * 100), settings.units_alt);
    auto gpsalt_end_formatted = baro_altToUnits(int32_t(gpsalt_end * 100), settings.units_alt);
    auto gpsalt_above_launch_max_formatted =
        baro_altToUnits(int32_t(gpsalt_above_launch_max * 100), settings.units_alt);

    String stringClimbMax;
    String stringClimbMin;
    if (settings.units_climb) {
      stringClimbMax = String(baro_climbToUnits(climb_max, settings.units_climb), 0);
      stringClimbMin = String(baro_climbToUnits(climb_min, settings.units_climb), 0);
    } else {
      stringClimbMax = String(baro_climbToUnits(climb_max, settings.units_climb), 1);
      stringClimbMin = String(baro_climbToUnits(climb_min, settings.units_climb), 1);
    }

    String speed_max_formatted;
    if (settings.units_speed) {
      speed_max_formatted = String(speed_max * 2.23694, 1) + " mph";
    } else {
      speed_max_formatted = String(speed_max * 3.6, 1) + " kph";
    }

    String distanceAlongPath_formatted;
    if (settings.units_distance) {
      if (distanceAlongPath > 800) {
        distanceAlongPath_formatted = String(distanceAlongPath * 0.000621371, 2) + " miles";
      } else {
        distanceAlongPath_formatted = String(distanceAlongPath * 3.28084, 0) + " ft";
      }
    } else {
      if (distanceAlongPath > 1000) {
        distanceAlongPath_formatted = String(distanceAlongPath / 1000, 2) + " km";
      } else {
        distanceAlongPath_formatted = String(distanceAlongPath, 0) + " m";
      }
    }
    String distanceStraightLine_formatted;
    if (settings.units_distance) {
      if (distanceStraightLine > 800) {
        distanceStraightLine_formatted = String(distanceStraightLine * 0.000621371, 2) + " miles";
      } else {
        distanceStraightLine_formatted = String(distanceStraightLine * 3.28084, 0) + " ft";
      }
    } else {
      if (distanceStraightLine > 1000) {
        distanceStraightLine_formatted = String(distanceStraightLine / 1000, 2) + " km";
      } else {
        distanceStraightLine_formatted = String(distanceStraightLine, 0) + " m";
      }
    }

    String trackdescription =
        "Alt:" + alt_units + " Start: " + gpsalt_start_formatted + " Max: " + gpsalt_max_formatted +
        "\n" + "      Above Launch: " + gpsalt_above_launch_max_formatted +
        " End: " + gpsalt_end_formatted + "\n" + "Climb " + climb_units +
        " Max: " + stringClimbMax + " Min: " + stringClimbMin + "\n" + "Temp " + temp_units +
        " Max: " + temperature_max + " Min: " + temperature_min + "\n" +
        "Max Speed: " + speed_max_formatted + "\n" +
        "Distance Along Path: " + distanceAlongPath_formatted + "\n" +
        "Straight Line Distance: " + distanceStraightLine_formatted + "\n";
    return trackdescription;
  }
};
