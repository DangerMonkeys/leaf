#include "telemetry.h"

#include <SD_MMC.h>

#include "Arduino.h"
#include "gps.h"
#include "log.h"
#include "settings.h"

bool Telemetry_t::begin() {
  // Get the local time
  tm cal;
  if (!gps.getLocalDateTime(cal)) {
    return false;
  }

  // format with strftime format.  Eg FlightTrack_2025-01-08_2301
  char fileName[60];
  String formatString = "/Data/TestData_%F_%H%M%S.csv";
  strftime(fileName, sizeof(fileName), formatString.c_str(), &cal);

  // Open the file to write the telemetry
  file = SD_MMC.open(fileName, "w", true);
  if (!file) {
    return false;
  }

  // Write the CSV header
  file.println("millis,sensor, value (lat), lng, alt m, speed mps, heading deg");
  return true;
}

void Telemetry_t::end() { file.close(); }

void Telemetry_t::writeText(const String text) {
  // Only write telemetry if a flight has started
  if (!flightTimer_isRunning()) return;

  // Try to start the telemetry file if not started
  if (!file) {
    begin();
    if (!file) return;
  }
  file.println((String)millis() + "," + text);
}

Telemetry_t Telemetry;