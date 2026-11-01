#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <fstream>

#include <string>
#include <vector>
#include "diagnostics/flight_diagnostics.h"
#include "dispatch/message_bus.h"
#include "display_capture.h"
#include "instruments/baro.h"
#include "instruments/imu.h"
#include "logbook/igc.h"
#include "logbook/logbook_entry.h"
#include "logbook/logbook_store.h"
#include "logging/flight_diagnostic_output.h"
#include "logging/log.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/pages/primary/page_user.h"
#include "ui/settings/settings.h"
#include "utils/string_utils.h"

extern MessageBus<11> bus;
extern Igc igcFlight;
extern LogbookEntryFile logbookEntry;

static void runFor(uint32_t ms) {
  const uint32_t start = millis();
  while (uint32_t(millis() - start) < ms) sim::runtime().step();
}

static void saveLoadScreen(const char* path) {
  u8g2.firstPage();
  do {
    u8g2.setDrawColor(1);
    drawUserField(15, 100, static_cast<uint8_t>(UserPageFields::ACCEL), false);
  } while (u8g2.nextPage());
  const auto png = sim::encodePng(sim::captureFrame(), 4);
  std::ofstream out(path, std::ios::binary);
  out.write(png.data(), png.size());
  assert(out.good());
}

static void feed(bool gravityMode, double ax, double az, bool clipped = false) {
  sim::clock().advanceUs(50000);
  MotionUpdate m(millis());
  m.hasAcceleration = true;
  m.ax = ax;
  m.az = az;
  m.hasOrientation = !gravityMode;
  m.hasGravity = gravityMode;
  m.gz = 1;
  m.clippedSinceLastUpdate = clipped;
  imu.on_receive(m);
}

int main() {
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/g-load/state";
  options.cardRoot = "sim/build-tests/g-load/card";
  sim::runtime().configure(options);
  sim::runtime().boot();
  settings.loadDefaults();
  settings.log_autoStart = settings.log_autoStop = false;
  settings.log_saveTrack = true;
  settings.system_autoOff = 0;
  settings.system_showWarning = false;

  struct DelayedPrint : Print {
    uint32_t delayMs = 0;
    std::string bytes;
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t* data, size_t size) override {
      sim::clock().advanceUs(uint64_t(delayMs) * 1000);
      bytes.append(reinterpret_cast<const char*>(data), size);
      return size;
    }
  } output;
  FlightDiagnosticOutput measured(output);
  flightDiagnostics.begin(millis());
  output.delayMs = 99;
  measured.write("fast");
  std::vector<std::string> events;
  auto capture = [&](const char* line) { events.emplace_back(line); };
  flightDiagnostics.report(millis(), capture, true);
  assert(events.empty());
  const uint32_t slowAt = millis();
  output.delayMs = 100;
  measured.write("slow");
  flightDiagnostics.report(millis(), capture, true);
  assert(events.size() == 1);
  assert(events[0] == "FD K=WRITE N=1 T=" + std::to_string(slowAt) + " DT=100 R=0");
  assert(output.bytes == "fastslow");
  flightDiagnostics.end();

  // Real instrument paths, including their sensor-specific validation and reset behavior.
  baro.sleep();
  for (bool gravityMode : {false, true}) {
    imu.wake();
    imu.gLoad().takeInterval();
    for (int i = 0; i < 40; ++i) feed(gravityMode, 0, 2.5, gravityMode && i == 10);
    assert(imu.accelValid() && imu.gLoad().filteredValid(millis()));
    assert(fabsf(imu.gLoad().filtered() - 2.5f) < .001f);
    saveLoadScreen("sim/build-tests/g-load/filtered.png");
    const auto q = imu.gLoad().takeInterval();
    assert(q.samples == 40);
    assert(q.clippingReports == (gravityMode ? 1 : 0));
    imu.wake();
    feed(gravityMode, 0, 1);
    assert(imu.accelValid() && !imu.gLoad().filteredValid(millis()));
  }
  // The LSM's legal per-axis values can produce a magnitude above 8g.
  imu.wake();
  imu.gLoad().takeInterval();
  feed(true, 6, 6);
  assert(imu.getAccel() > 8);
  assert(imu.gLoad().takeInterval().samples == 1);
  // Reset provenance comes from the real instrument branches, not just tracker calls.
  auto reason = [](GLoadTracker::ResetReason r) { return static_cast<uint16_t>(r); };
  MotionUpdate reset(millis());
  reset.reset = true;
  imu.on_receive(reset);
  assert(imu.gLoad().takeInterval().resetReasons ==
         reason(GLoadTracker::ResetReason::SensorMessage));
  feed(true, 0, 1);
  imu.gLoad().takeInterval();
  feed(false, 0, 1);
  assert(imu.gLoad().takeInterval().resetReasons ==
         reason(GLoadTracker::ResetReason::SourceChange));
  sim::clock().advanceUs(50000);
  MotionUpdate missing(millis());
  missing.hasGravity = true;
  missing.gz = 1;
  imu.on_receive(missing);
  assert(imu.gLoad().takeInterval().resetReasons ==
         reason(GLoadTracker::ResetReason::MissingAcceleration));
  sim::clock().advanceUs(50000);
  MotionUpdate invalid(millis());
  invalid.hasGravity = invalid.hasAcceleration = true;
  invalid.az = 1;
  imu.on_receive(invalid);
  assert(imu.gLoad().takeInterval().resetReasons ==
         reason(GLoadTracker::ResetReason::InvalidGravity));
  assert(formatAccel(NAN, false) == "--");
  assert(formatAccel(13.8f, true) == "13.8g");
  settings.disp_userPageUser1 = static_cast<uint8_t>(UserPageFields::ACCEL);
  userPage_draw();  // Missing filtered data is drawable during recovery.
  saveLoadScreen("sim/build-tests/g-load/unavailable.png");

  baro.wake();
  sim::scenario().setBus(&bus);
  std::string error;
  assert(sim::scenario().load("sim/recordings/thermal-climb.json", error));
  sim::scenario().play();
  runFor(30000);

  // A short flight persists null extrema, including through the summary reader.
  flightTimer_start();
  runFor(10000);
  const String shortEntry = logbookEntry.path();
  assert(igcFlight.started());
  flightTimer_stop(false);
  JsonDocument doc;
  File shortFile = SD_MMC.open(shortEntry.c_str());
  assert(!deserializeJson(doc, shortFile));
  assert(doc["metrics"]["max_accel_g"].isNull());
  assert(doc["metrics"]["min_accel_g"].isNull());
  LogbookEntrySummary shortSummary;
  assert(LogbookStore::readSummary(shortEntry, shortSummary));
  assert(isnan(shortSummary.maxAccelG) && isnan(shortSummary.minAccelG));

  // A long flight still commits extrema, without the eleven per-second diagnostic records.
  flightTimer_start();
  runFor(130000);
  const String entryPath = logbookEntry.path();
  const String trackPath = igcFlight.trackLogPath();
  assert(imu.gLoad().valid());
  MotionUpdate sensorReset(millis());
  sensorReset.reset = true;
  imu.on_receive(sensorReset);
  flightTimer_stop(false);
  File entry = SD_MMC.open(entryPath.c_str());
  assert(!deserializeJson(doc, entry));
  assert(doc["metrics"]["max_accel_g"].is<float>());
  File track = SD_MMC.open(trackPath.c_str());
  const String contents = track.readString();
  assert(contents.indexOf("LXLVGL") < 0 && contents.indexOf("LXLVGR ") < 0);
  assert(contents.indexOf("LXLVSTARTDATA") < 0 && contents.indexOf("LXLVSTARTPOS") < 0);
  assert(contents.indexOf("LXLVFDCFG V=1") >= 0);
  assert(contents.indexOf("LXLVFD K=IMU N=1") >= 0);
  assert(contents.indexOf("LXLVEND V=1") >= 0);
  assert(contents.indexOf("LXLVEND V=1") < contents.lastIndexOf("\nG"));
  assert(contents.length() < 20000);  // Old verbose diagnostics added roughly 85 kB here.
  puts("Both IMUs, live recovery, short-flight nulls and compact IGC checks passed");
}
