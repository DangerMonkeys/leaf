#include "logging/buslog.h"

#include <SD_MMC.h>
#include <time.h>

#include "file_writer.h"
#include "instruments/gps.h"
#include "storage/sd_card.h"
#include "system/version_info.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"

BusLogger busLog;

void BusLogger::statsCallback(TimerHandle_t x) {
  // Prints stats to the file.
  busLog.file_.printf("S%d,%d\n", millis() - busLog.tStart_,
                      AsyncLogger::getFreeSizeLowWatermark());
}

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

  AsyncLogger::enqueuef(&file_, "V%s\n", LeafVersionInfo::firmwareVersion());

  tStart_ = millis();

  // Start a periodic task to write watermarks every 10 seconds
  statTimer_ = xTimerCreate("StatsTimer", pdMS_TO_TICKS(10000), pdTRUE, NULL, statsCallback);
  if (statTimer_ != nullptr) {
    xTimerStart(statTimer_, 0);  // 0 = no block waiting for command
  }

  return true;
}

void BusLogger::on_receive(const AmbientUpdate& msg) {
  if (!file_) return;
  AsyncLogger::enqueuef(&file_, "A%d,%f,%f\n", millis() - tStart_, msg.temperature,
                        msg.relativeHumidity);
}

void BusLogger::on_receive(const CommentMessage& msg) {
  if (!file_) return;
  AsyncLogger::enqueuef(&file_, "#%d,%s\n", millis() - tStart_, msg.message);
}

void BusLogger::on_receive(const GpsMessage& msg) {
  if (!file_) return;
  AsyncLogger::enqueuef(&file_, "G%d,%s\n", millis() - tStart_, msg.nmea.c_str());
}

void BusLogger::on_receive(const MotionUpdate& msg) {
  if (!file_) return;
  AsyncLogger::enqueuef(&file_, "M%d,%s,%g,%g,%g,%s,%g,%g,%g\n", msg.t - tStart_,
                        msg.hasAcceleration ? "A" : "a", msg.ax, msg.ay, msg.az,
                        msg.hasOrientation ? "Q" : "q", msg.qx, msg.qy, msg.qz);
}

void BusLogger::on_receive(const PressureUpdate& msg) {
  if (!file_) return;
  AsyncLogger::enqueuef(&file_, "P%d,%d\n", msg.t - tStart_, msg.pressure);
}

void BusLogger::endLog() {
  // Delete the statistic writer
  xTimerDelete(statTimer_, pdMS_TO_TICKS(10));
  AsyncLogger::flush();  // Flush any existing bus logs to the file

  if (bus_) {
    bus_->unsubscribe(*this);
  }
  if (file_) {
    file_.close();
  }
}
