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
  // First format and print the GPS coordinate
  char lonStr[20];
  char latStr[20];
  char altStr[20];
  char logPointStr[80];

  // Format each value with the desired precision
  snprintf(lonStr, sizeof(lonStr), "%.7f", gps.location.lng());
  snprintf(latStr, sizeof(latStr), "%.7f", gps.location.lat());
  snprintf(altStr, sizeof(altStr), "%.2f", gps.altitude.meters());

  // Combine into final string
  snprintf(logPointStr, sizeof(logPointStr), "<gx:coord>%s %s %s</gx:coord>", lonStr, latStr,
           altStr);

  file.println(logPointStr);

  // then format and print the timestamp
  char whenPointStr[40];

  snprintf(whenPointStr, sizeof(whenPointStr), "<when>%04d-%02d-%02dT%02d:%02d:%02dZ</when>",
           gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(),
           gps.time.second());

  file.println(whenPointStr);
}

void Kml::end(const FlightStats stats, bool showSummary) {
  file.println(KMLtrackFooterA);  // close </gxtrack> and open track <name>
  file.println("Flight Time: " + formatSeconds(stats.duration, false, 0));
  file.println(KMLtrackFooterB);  // close track </name> and open track <description>
  file.println(stats.toString());
  file.println(KMLtrackFooterC);  // close </description> and </placemark> and open document <name>
  file.println(file.name());  // KML file title (in google earth) same as long file name on SDcard
  file.println(KMLtrackFooterD);  // close document </name> and open document <description>
  // skipping KML file description.  Not needed and clogs up the google earth places list
  file.println(KMLtrackFooterE);  // close document </description> and close document and kml tags
  Flight::end(stats, showSummary);
}
