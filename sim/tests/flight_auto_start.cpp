#include <Arduino.h>
#include <SD_MMC.h>
#include <assert.h>
#include <stdio.h>

#include <cmath>
#include <limits>
#include <string>

#include "dispatch/message_bus.h"
#include "dispatch/message_injector.h"
#include "flight_start_window_checks.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logbook/igc.h"
#include "logging/flight_auto_start.h"
#include "logging/flight_start_history.h"
#include "logging/log.h"
#include "power.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/menu_page.h"
#include "ui/display/pages/dialogs/page_flight_summary.h"
#include "ui/settings/settings.h"

extern MessageBus<11> bus;
extern Igc igcFlight;

namespace {
  MessageInjector injector;
  sim::FlightSensorState sensor;
  uint32_t sensorAtMs = 0;
  uint32_t gpsAtMs = 0;

  void fly(unsigned durationMs, float speedMph = 0, float climbMps = 0, bool withGps = true) {
    const uint32_t started = millis();
    while (uint32_t(millis() - started) < durationMs) {
      const uint32_t now = millis();
      if (uint32_t(now - sensorAtMs) >= 50) {
        const double seconds = uint32_t(now - sensorAtMs) / 1000.0;
        sensor.altitudeM += climbMps * seconds;
        sensor.latitude += speedMph * .44704 * seconds / 111195.0;
        sensorAtMs = now;
        sensor.atMs = now;
        sensor.northMps = speedMph * .44704;
        const bool gpsDue = withGps && uint32_t(now - gpsAtMs) >= 1000;
        if (gpsDue) gpsAtMs = now;
        for (const auto& line : sim::flightSensorLines(sensor, gpsDue, gpsDue))
          assert(injector.handleLine(line.c_str(), line.size()));
      }
      sim::runtime().step();
    }
  }

  std::string readTrack(const String& path) {
    File f = SD_MMC.open(path.c_str());
    assert(f);
    const std::string text = f.readString().c_str();
    f.close();
    assert(text.find("LXLVSTART V=2 CAUSE=") != std::string::npos);
    assert(text.find("LXLVSTARTCFG WINDOW_MS=") != std::string::npos);
    assert(text.find("LXLVSTARTUNITS V=M/S C=M/S A=M") != std::string::npos);
    assert(text.find("LXLVSTARTALT ") != std::string::npos);
    assert(text.find("LXLVSTARTPATH ") != std::string::npos);
    for (size_t start = 0; start < text.size();) {
      const size_t end = text.find('\n', start);
      if (end == std::string::npos) break;
      const size_t length = end - start - (end > start && text[end - 1] == '\r' ? 1 : 0);
      if (text.compare(start, 9, "LXLVSTART") == 0) assert(length <= 76);
      start = end + 1;
    }
    size_t rows = 0, position = 0;
    while ((position = text.find("LXLVSTARTDATA ", position)) != std::string::npos) {
      ++rows;
      ++position;
    }
    assert(rows > 0 && rows <= FlightStartHistory::CAPACITY);
    // Every normal close includes the end cause before the final security record.
    assert(text.rfind("\nG") != std::string::npos);
    assert(text.find("LXLVEND V=1 CAUSE=") < text.rfind("\nG"));
    return text;
  }

  void checkFirmware() {
    sim::Runtime::Options options;
    options.httpPort = 0;
    options.speed = 0;
    options.acceptWarning = true;
    options.exitOnFatalError = true;
    options.statePath = "sim/build-tests/flight-auto-start/state";
    options.cardRoot = "sim/build-tests/flight-auto-start/card";
    sim::runtime().configure(options);
    sim::runtime().boot();
    settings.loadDefaults();
    settings.log_autoStart = settings.log_autoStop = false;
    settings.log_saveTrack = true;
    settings.system_autoOff = 0;
    settings.system_showWarning = false;
    injector.setBus(&bus);
    sensor.altitudeM = 1000;
    sensorAtMs = millis();
    fly(40000, 0, 0, false);
    assert(baro.climbRate1SecAverageValid());
    assert(!gps.hasUsableFix());

    // A manual pre-GPS start freezes history through a long wait for the file.
    const unsigned manualAt = millis();
    flightTimer_start();
    assert(flightTimer_isRunning() && !igcFlight.started());
    fly(45000, 0, 0, false);
    assert(!igcFlight.started());
    fly(2500);
    assert(igcFlight.started());
    const String manualPath = igcFlight.trackLogPath();
    flightTimer_stop(false);
    auto text = readTrack(manualPath);
    const std::string header = "START V=2 CAUSE=MANUAL T_MS=" + std::to_string(manualAt);
    assert(text.find(header) != std::string::npos);
    assert(text.find(" N=30") != std::string::npos);
    assert(text.find(" V=NA ") != std::string::npos);
    assert(text.find("END V=1 CAUSE=MANUAL") != std::string::npos);

    // Disabled detection cannot leave evidence to trigger immediately on enable.
    fly(8000, 8);
    assert(!flightTimer_isRunning());
    settings.toggleBoolOnOff(&settings.log_autoStart);
    fly(3000, 8);
    assert(!flightTimer_isRunning());
    // A fast off/on between scheduled checks must discard partial launch evidence.
    settings.toggleBoolOnOff(&settings.log_autoStart);
    settings.toggleBoolOnOff(&settings.log_autoStart);
    fly(2000, 8);
    assert(!flightTimer_isRunning());
    fly(4000, 8);
    assert(flightTimer_isRunning() && igcFlight.started());
    const String autoPath = igcFlight.trackLogPath();
    settings.toggleBoolOnOff(&settings.log_autoStart);
    assert(flightTimer_isRunning());
    settings.toggleBoolOnOff(&settings.log_autoStart);
    assert(flightTimer_isRunning());
    flightTimer_start();  // An accidental duplicate must not rewrite AUTO to MANUAL.
    settings.log_autoStop = true;
    fly(30000);
    assert(!flightTimer_isRunning() && PageFlightSummary::isShowing());
    text = readTrack(autoPath);
    assert(text.find("START V=2 CAUSE=AUTO") != std::string::npos);
    assert(text.find("END V=1 CAUSE=AUTO") != std::string::npos);
    assert(text.find("LXLVSTARTPOS ") != std::string::npos);
    assert(text.find(" Q=01 T=01") != std::string::npos);
    fly(8000, 8);
    assert(!flightTimer_isRunning());
    PageFlightSummary summary;
    summary.pop_all_pages();
    assert(!PageFlightSummary::isShowing());

    // The actual fused baro/IMU signal can trigger a vertical launch without GPS.
    settings.log_autoStart = settings.log_autoStop = false;
    fly(5000, 0, 0, false);
    assert(!gps.hasFreshGroundSpeed());
    settings.log_autoStart = true;
    const uint32_t launchAt = millis();
    while (!flightTimer_isRunning() && uint32_t(millis() - launchAt) < 10000)
      fly(100, 0, 1.2f, false);
    assert(flightTimer_isRunning() && !igcFlight.started());
    printf("Fused climb-only launch: %.2f seconds after climb onset\n",
           (millis() - launchAt) / 1000.0);
    const unsigned climbStartAt = logbook.logStartedAt;
    fly(12000, 0, 1.2f, false);
    fly(2500, 0, 1.2f, true);
    assert(igcFlight.started());
    assert(logbook.logStartedAt == climbStartAt);
    const String climbPath = igcFlight.trackLogPath();
    flightTimer_stop(false);
    text = readTrack(climbPath);
    assert(text.find("START V=2 CAUSE=AUTO") != std::string::npos);
    assert(text.find(" V=NA ") != std::string::npos);

    // Actual descending pressure and fused velocity use the same vertical path.
    settings.log_autoStart = false;
    fly(6000, 0, 0, false);
    settings.log_autoStart = true;
    const uint32_t descentAt = millis();
    while (!flightTimer_isRunning() && uint32_t(millis() - descentAt) < 10000)
      fly(100, 0, -1.2f, false);
    assert(flightTimer_isRunning() && !igcFlight.started());
    fly(2500, 0, -1.2f, true);
    assert(igcFlight.started());
    const String descentPath = igcFlight.trackLogPath();
    flightTimer_stop(false);
    text = readTrack(descentPath);
    assert(text.find(" Q=02 T=02") != std::string::npos);

    // Below both standalone thresholds, real pressure/IMU/GPS injection starts
    // through the blended path. The idle fixes populate its spatial window.
    settings.log_autoStart = false;
    fly(6000);
    settings.log_autoStart = true;
    const uint32_t mixedAt = millis();
    while (!flightTimer_isRunning() && uint32_t(millis() - mixedAt) < 10000) fly(100, 3.75f, .6f);
    assert(flightTimer_isRunning() && igcFlight.started());
    const String mixedPath = igcFlight.trackLogPath();
    flightTimer_stop(false);
    text = readTrack(mixedPath);
    assert(text.find(" Q=04 T=04") != std::string::npos);

    settings.log_autoStart = false;
    fly(1500);
    flightTimer_start();
    fly(1500);
    const String shutdownPath = igcFlight.trackLogPath();
    assert(igcFlight.started());
    power.shutdown(false);
    assert(!flightTimer_isRunning());
    text = readTrack(shutdownPath);
    assert(text.find("END V=1 CAUSE=POWER_OFF") != std::string::npos);
  }
}  // namespace

int main() {
  flight_start_test::checkFlightStartWindows();
  checkFirmware();
  puts(
      "Launch windows, sensor corroboration, circular history, delayed GPS, IGC causes "
      "and lifecycle passed");
}
