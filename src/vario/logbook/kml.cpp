#include "kml.h"

#include "instruments/gps.h"
#include "time.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"

const String Kml::desiredFileName() const {
  // Get the local time
  tm cal;
  gps.getLocalDateTime(cal);

  // format with strftime format.  Eg FlightTrack_2025-01-08_2301
  char fileString[60];
  String formatString = "FlightTrack_%F_%H%M";
  strftime(fileString, sizeof(fileString), formatString.c_str(), &cal);

  return fileString;
}

bool Kml::startFlight() {
  if (!Flight::startFlight()) return false;
  file.println(KMLtrackHeader);
  return true;
}

void Kml::log(unsigned long durationSec) {
  String lonPoint = String(gps.location.lng(), 7);
  String latPoint = String(gps.location.lat(), 7);
  String altPoint = String(gps.altitude.meters(), 2);
  String logPointStr =
      "<gx:coord>" + lonPoint + " " + latPoint + " " + altPoint + "</gx:coord>" + "\n";

  file.println(logPointStr);

  String timeYear = String(gps.date.year());
  String timeMonth = String(gps.date.month());
  String timeDay = String(gps.date.day());
  String timeHour = String(gps.time.hour());
  String timeMinute = String(gps.time.minute());
  String timeSecond = String(gps.time.second());
  String whenPointStr = "<when>" + timeYear + "-" + timeMonth + "-" + timeDay + "T" + timeHour +
                        ":" + timeMinute + ":" + timeSecond + "Z</when>" + "\n";
  file.println(whenPointStr);
}

void Kml::end(const FlightStats stats) {
  file.println(KMLtrackFooterA);
  file.println("Flight Time: " + formatSeconds(stats.duration, false, 0));
  file.println(KMLtrackFooterB);
  file.println(stats.toString());
  file.println(KMLtrackFooterC);
  file.println(file.name());  // KML file title (in google earth) same as long file name on SDcard
  file.println(KMLtrackFooterD);
  // skipping KML file description.  Not needed and clogs up the google earth places list
  file.println(KMLtrackFooterE);
  Flight::end(stats);
}