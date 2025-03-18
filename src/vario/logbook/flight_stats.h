#pragma once

#include "Arduino.h"
#include "baro.h"
#include "settings.h"

class FlightStats {
 public:
  int32_t alt = 0;  // alt in cm
  int32_t alt_start = 0;
  int32_t alt_end = 0;
  int32_t alt_max = 0;
  int32_t alt_min = 0;
  int32_t alt_above_launch = 0;
  int32_t alt_above_launch_max = 0;

  int32_t climb = 0;  // climb in cm/s
  int32_t climb_max = 0;
  int32_t climb_min = 0;

  float temperature = 0;
  float temperature_max = 0;
  float temperature_min = 0;

  float speed = 0;      // speed in mps
  float speed_max = 0;  // max speed logged in mps

  float distanceFlown = 0;  // accumulated distance (m) of actual flight path

  float accel = 1;
  float accel_max = 1;
  float accel_min = 1;

  // Flight duration, in seconds
  unsigned long duration = 0;
  unsigned long logStartedAt = 0;  // Time flight started (seconds started since boot)

  String toString() const {
    Serial.print("creating track description...");

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

    String stringClimbMax;
    String stringClimbMin;
    if (settings.units_climb) {
      stringClimbMax = String(baro_climbToUnits(climb_max, settings.units_climb), 0);
      stringClimbMin = String(baro_climbToUnits(climb_min, settings.units_climb), 0);
    } else {
      stringClimbMax = String(baro_climbToUnits(climb_max, settings.units_climb), 1);
      stringClimbMin = String(baro_climbToUnits(climb_min, settings.units_climb), 1);
    }

    String trackdescription =
        "Altitude " + alt_units + " Start: " + alt_start_formatted +
        " Max: " + alt_start_formatted + " Above Launch: " + alt_above_launch_max_formatted +
        " Min: " + alt_min_formatted + " End: " + alt_end_formatted + "\n" + "Climb " +
        climb_units + " Max: " + stringClimbMax + " Min: " + stringClimbMin + "\n" + "Temp " +
        temp_units + " Max: " + temperature_max + " Min: " + temperature_min + "\n" +
        "Max Speed: 41 mph\n" + "Distance (mi) Direct: 2.3 Path: 35.1\n";

    Serial.println("finished");

    return trackdescription;
  }
};
