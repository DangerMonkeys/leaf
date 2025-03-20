#include "flight.h"

#include <SD_MMC.h>

#include "FS.h"
#include "logging/telemetry.h"
#include "storage/sd_card.h"
#include "ui/display/pages/dialogs/page_flight_summary.h"

bool Flight::startFlight() {
  // Short circuit if the card is not mounted or reading properly
  if (!SDcard_present()) return false;
  Serial.printf("Sectors: %d\n", SD_MMC.numSectors());
  File trackLogsDir = SD_MMC.open(this->desiredFilePath());

  auto filePrefix = this->desiredFilePath() + "/" + this->desiredFileName();
  auto suffix = this->fileNameSuffix();
  // Perform a directory listing of all the file in the tracks directory
  auto nextFile = trackLogsDir.getNextFileName();
  auto desiredFlightNum = 1;
  while (!nextFile.isEmpty()) {
    if (!nextFile.endsWith(suffix) || !nextFile.startsWith(filePrefix)) {
      nextFile = trackLogsDir.getNextFileName();
      continue;
    }

    // This file matches the format we're after.  Find the flight number
    // igc-file-01.igc should match 01
    auto flightNumStr =
        nextFile.substring(filePrefix.length() + 1, nextFile.length() - suffix.length());
    auto flightNum = flightNumStr.toInt();
    if (flightNum >= desiredFlightNum) {
      desiredFlightNum = flightNum + 1;
    }

    nextFile = trackLogsDir.getNextFileName();
  }

  String fileName = filePrefix + "-" +
                    (desiredFlightNum < 10 ? String(0) + desiredFlightNum : desiredFlightNum) +
                    "." + suffix;

  // Create the file for writing
  file = SD_MMC.open(fileName, "w", true);

  return true;
}

void Flight::end(const FlightStats stats) {
  file.close();
  Telemetry.end();  // End and flush telemetry

  // Finally, show the flight summary page
  static PageFlightSummary dialog;
  dialog.show(stats);
}

bool Flight::started() { return (boolean)file; }