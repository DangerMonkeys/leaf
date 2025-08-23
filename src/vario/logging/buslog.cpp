#include "logging/buslog.h"

#include <SD_MMC.h>
#include <time.h>

#include "instruments/gps.h"
#include "leaf_version.h"
#include "storage/sd_card.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"

BusLogger busLog;

namespace {
  constexpr char* BUSLOG_PATH = "/buslogs";
  constexpr char* BUSLOG_EXTENSION = ".log";
}  // namespace

String BusLogger::desiredFileName() const {
  tm cal;
  bool hasDateTime = gps.getLocalDateTime(cal);
  if (!hasDateTime) return "BusLog";

  char fileString[60];
  strftime(fileString, sizeof(fileString), "BusLog_%F_%H%M", &cal);

  return String(fileString);
}

bool BusLogger::startLog() {
  if (!sdcard.isMounted()) {
    Serial.println("BusLogger::startLog failed: sdcard not mounted");
    return false;
  }
  if (!bus_) {
    Serial.println("BusLogger::startLog failed: no bus available");
    return false;
  }

  String filePrefix = String(BUSLOG_PATH) + "/" + desiredFileName();

  String fileName = filePrefix + BUSLOG_EXTENSION;
  if (SD_MMC.exists(fileName)) {
    // Deconflict from existing file (this should be rare)
    int i = 1;
    do {
      fileName = filePrefix + "-" + i + BUSLOG_EXTENSION;
      i++;
    } while (SD_MMC.exists(fileName));
  }

  file_ = SD_MMC.open(fileName, "w", true);
  if (!file_) {
    Serial.println("BusLogger::startLog failed: couldn't open" + fileName);
    return false;
  }

  bus_->unsubscribe(*this);  // Make sure we don't double-subscribe
  if (!bus_->subscribe(*this)) {
    Serial.println("BusLogger::startLog failed: subscribe to bus");
    file_.close();
    return false;
  }

  file_.printf("V%s\n", FIRMWARE_VERSION);

  tStart_ = millis();

  return true;
}

void BusLogger::on_receive(const AmbientUpdate& msg) {
  if (!file_) return;
  file_.printf("A%d,%f,%f\n", millis() - tStart_, msg.temperature, msg.relativeHumidity);
}

void BusLogger::on_receive(const CommentMessage& msg) {
  if (!file_) return;
  file_.printf("#%d,%s\n", millis() - tStart_, msg.message);
}

void BusLogger::on_receive(const GpsMessage& msg) {
  if (!file_) return;
  file_.printf("G%d,%s\n", millis() - tStart_, msg.nmea.c_str());
}

void BusLogger::on_receive(const MotionUpdate& msg) {
  if (!file_) return;
  file_.printf("M%d,%s,%g,%g,%g,%s,%g,%g,%g\n", msg.t - tStart_, msg.hasAcceleration ? "A" : "a",
               msg.ax, msg.ay, msg.az, msg.hasOrientation ? "Q" : "q", msg.qx, msg.qy, msg.qz);
}

void BusLogger::on_receive(const PressureUpdate& msg) {
  if (!file_) return;
  file_.printf("P%d,%d\n", msg.t - tStart_, msg.pressure);
}

void BusLogger::endLog() {
  if (bus_) {
    bus_->unsubscribe(*this);
  }
  if (file_) {
    file_.close();
  }
}
