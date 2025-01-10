#include "flight.h"

#include <SD_MMC.h>

#include "FS.h"
#include "telemetry.h"

void Flight::startFlight() {
  File trackLogsDir = SD_MMC.open(this->desiredFilePath());

  auto filePrefix = this->desiredFileName();
  auto suffix = this->fileNameSuffix();
  // Perform a directory listing of all the file in the tracks directory
  auto nextFile = trackLogsDir.getNextFileName();
  auto desiredFlightNum = 1;
  while (!nextFile.isEmpty()) {
    Serial.println("Doing dir listing");
    if (!nextFile.endsWith(suffix) || !nextFile.startsWith(filePrefix)) {
      nextFile = trackLogsDir.getNextFileName();
      continue;
    }

    // This file matches the format we're after.  Find the flight number
    // igc-file-01.igc should match 01
    auto flightNumStr =
        nextFile.substring(filePrefix.length() + 1, nextFile.length() - suffix.length());
    auto flightNum = flightNumStr.toInt();
    if (flightNum > desiredFlightNum) {
      desiredFlightNum = flightNum + 1;
    }

    nextFile = trackLogsDir.getNextFileName();
  }

  String fileName = this->desiredFilePath() + "/" + filePrefix + "-" +
                    (desiredFlightNum < 10 ? String(0) + desiredFlightNum : desiredFlightNum) +
                    "." + suffix;

  Serial.println((String) "Recording flight: " + fileName);

  // Create the file for writing
  file = SD_MMC.open(fileName, "w", true);
}

void Flight::end(const FlightStats stats) {
  file.close();
  Telemetry.end();  // End and flush telemetry
}

bool Flight::started() {
  return (boolean)file;
}