#pragma once

#include "Arduino.h"
#include "FS.h"
#include "flight_stats.h"

class Flight {
 public:
  // We wish to start recording a flight
  virtual void startFlight();
  virtual void end(const FlightStats stats);

  // Logs a datapoint
  virtual void log(unsigned long durationSec) {}

  bool started();

 protected:
  // eg. igc, kml
  virtual const String fileNameSuffix() const = 0;

  // The desired filename to log.  In the instance of there being two
  // files logged with the same filename, then we'll append a two digit
  // "flight of the day" number to the file (as per .igc spec)
  virtual const String desiredFileName() const = 0;

  // Directory where the track log is to be stored
  virtual const String desiredFilePath() const { return "/tracks"; }

  File file;
};
