#include "logbook/bus.h"

#include <time.h>

#include "instruments/gps.h"
#include "leaf_version.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"

const String Bus::desiredFileName() const {
  // Get the local time
  tm cal;
  gps.getLocalDateTime(cal);

  char fileString[60];
  String formatString = "BusLog_%F_%H%M";
  strftime(fileString, sizeof(fileString), formatString.c_str(), &cal);

  return fileString;
}

bool Bus::startFlight() {
  if (!bus_) return false;
  if (!Flight::startFlight()) return false;
  if (!file) return false;

  file.printf("V%s\n", FIRMWARE_VERSION);

  tStart_ = millis();
  return bus_->subscribe(*this);
}

void Bus::on_receive(const AmbientUpdate& msg) {
  if (!file) return;
  file.printf("A%d,%f,%f\n", millis() - tStart_, msg.temperature, msg.relativeHumidity);
}

void Bus::on_receive(const GpsMessage& msg) {
  if (!file) return;
  file.printf("G%d,%d,%s\n", millis() - tStart_, msg.nmea.length(), msg.nmea.c_str());
}

void Bus::on_receive(const MotionUpdate& msg) {
  if (!file) return;
  file.printf("M%d,%s,%g,%g,%g,%s,%g,%g,%g\n", msg.t - tStart_, msg.hasAcceleration ? "A" : "a",
              msg.ax, msg.ay, msg.az, msg.hasOrientation ? "Q" : "q", msg.qx, msg.qy, msg.qz);
}

void Bus::on_receive(const PressureUpdate& msg) {
  if (!file) return;
  file.printf("P%d,%d\n", msg.t - tStart_, msg.pressure);
}

void Bus::end(const FlightStats stats) {
  bus_->unsubscribe(*this);
  Flight::end(stats);
}
