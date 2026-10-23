#include <Arduino.h>
#include <SD_MMC.h>
#include <assert.h>
#include <stdio.h>

#include <cmath>
#include <limits>
#include <string>

#include "dispatch/message_bus.h"
#include "dispatch/message_injector.h"
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
  using Sample = FlightAutoStart::Sample;

  unsigned sustained(const char* label, Sample sample, uint32_t period = 1000,
                     uint32_t origin = 0) {
    FlightAutoStart detector;
    assert(!detector.update(origin, sample));
    for (uint32_t elapsed = period; elapsed <= 20000; elapsed += period) {
      if (detector.update(origin + elapsed, sample)) {
        assert(elapsed >= 3000);
        printf("Launch calibration: %s = %.2f seconds\n", label, elapsed / 1000.0);
        return elapsed;
      }
    }
    assert(false && "Expected a launch within twenty seconds");
    return 0;
  }

  void checkModel() {
    assert(sustained("8 mph", {8, 0, true, false}) == 3000);
    for (float peak : {6.f, 7.f}) {
      FlightAutoStart d;
      assert(!d.update(0, {peak, 0, true, false}));
      assert(!d.update(1000, {peak, 0, true, false}));
      unsigned triggered = 0;
      for (unsigned t = 2000; t <= 10000; t += 1000) {
        if (d.update(t, {5, 0, true, false})) {
          triggered = t;
          break;
        }
      }
      printf("Launch calibration: %.0f mph once then 5 mph = %.2f seconds\n", peak,
             triggered / 1000.0);
      assert(triggered >= 3000 && triggered <= 6000);
    }
    assert(sustained("climb 1.2 m/s, no GPS", {0, 1.2f, false, true}) == 3000);
    assert(sustained("climb 1.0 m/s, no GPS", {0, 1.f, false, true}) <= 4000);
    assert(sustained("climb 0.8 m/s, no GPS", {0, .8f, false, true}) <= 6000);
    assert(sustained("4 mph + 0.4 m/s", {4, .4f, true, true}) <= 4000);
    assert(sustained("3 mph + 0.3 m/s", {3, .3f, true, true}) <= 6000);
    assert(sustained("extreme input, 250 ms updates", {1000, 1000, true, true}, 250) == 3000);
    assert(sustained("millis rollover", {8, 0, true, false}, 1000, UINT32_MAX - 1500) == 3000);

    // Neither weak activity, descending, exact equilibrium, nor invalid sensors
    // may eventually creep across the threshold, even after thirty minutes.
    for (Sample quiet : {Sample{3, 0, true, true},
                         {0, .5f, false, true},
                         {2, .2f, true, true},
                         {3, -10, true, true},
                         {FlightAutoStart::SPEED_SCALE_MPH, 0, true, false},
                         {50, 50, false, false},
                         {NAN, INFINITY, true, true}}) {
      FlightAutoStart d;
      for (unsigned t = 0; t <= 1800000; t += 1000) assert(!d.update(t, quiet));
    }

    FlightAutoStart d;
    assert(!d.update(0, {0, 0, true, true}));
    for (unsigned t = 1000; t <= 60000; t += 1000) {
      const bool spike = t % 5000 == 1000;
      assert(!d.update(t, {spike ? 1000.f : 0, 0, true, true}));
    }
    d.reset();
    assert(!d.update(0, {8, 0, true, false}));
    assert(!d.update(1000, {8, 0, true, false}));
    assert(!d.update(2000, {8, 0, true, false}));
    const float beforeDip = d.score();
    assert(!d.update(3000, {0, 0, true, false}));
    assert(d.score() > 0 && d.score() < beforeDip);
    assert(!d.update(4000, {8, 0, true, false}));
    assert(d.update(5000, {8, 0, true, false}));
    d.reset();
    assert(!d.update(0, {8, 0, true, false}));
    assert(!d.update(1000, {8, 0, true, false}));
    assert(!d.update(2000, {8, 0, true, false}));
    assert(!d.update(3000, {}));  // No available sensor resets the accumulated evidence.
    assert(d.score() == 0);
    assert(!d.update(4000, {8, 0, true, false}));
    assert(!d.update(5000, {8, 0, true, false}));
    assert(!d.update(6000, {8, 0, true, false}));
    assert(!d.update(20000, {8, 0, true, false}));  // Unknown time must not count.
    assert(d.score() == 0);
    assert(!d.update(21000, {8, 0, true, false}));
    assert(!d.update(22000, {8, 0, true, false}));
    assert(d.update(23000, {8, 0, true, false}));
  }

  void checkGpsDropouts() {
    FlightAutoStart d;
    assert(!d.update(0, {8, 0, true, true}));
    assert(!d.update(1000, {8, 0, true, true}));
    assert(!d.update(2000, {8, 0, true, true}));
    const float beforeLoss = d.score();
    // A cached high speed marked invalid must contribute nothing. With valid
    // flat climb remaining, the old evidence fades rather than being refreshed.
    for (unsigned t = 3000; t <= 10000; t += 1000) assert(!d.update(t, {1000, 0, false, true}));
    assert(d.score() > 0 && d.score() < beforeLoss / 10);
    assert(!d.update(11000, {8, 0, true, true}));
    assert(!d.update(12000, {8, 0, true, true}));
    assert(d.update(13000, {8, 0, true, true}));
    // Valid climb still works while GPS is absent; stale climb cannot assist speed.
    assert(sustained("stale GPS with valid climb", {1000, 1.2f, false, true}) == 3000);
    assert(sustained("fresh speed with stale climb", {8, 1000, true, false}) == 3000);
  }

  void checkHistory() {
    FlightStartHistory h;
    for (unsigned t = 0; t <= 34000; t += 1000)
      h.record(t, {float(t / 1000), -1, true, true}, .5f, true, false);
    assert(h.size() == 30);
    assert(h.at(0).atMs == 5000 && h.at(29).atMs == 34000);
    h.record(34500, {8, .4f, true, true}, 1, true, false);
    assert(h.size() == 30 && h.at(29).atMs == 34500 && h.at(29).score == 1);
    h.freeze(34500, FlightStartCause::Automatic);
    h.record(40000, {}, 0, false, true);
    assert(h.at(29).atMs == 34500 && h.cause() == FlightStartCause::Automatic);
    h.reset();
    h.record(UINT32_MAX - 100, {8, 0, true, false}, 0, false, false);
    h.record(900, {8, 0, true, false}, 0, false, false);
    assert(h.size() == 2);
    h.record(10000, {NAN, INFINITY, true, true}, 0, true, false);
    assert(h.size() == 1);
    assert(h.at(0).flags == FlightStartHistory::Enabled);
  }

  MessageInjector injector;
  sim::FlightSensorState sensor;
  uint32_t sensorAtMs = 0;
  uint32_t gpsAtMs = 0;

  void fly(unsigned durationMs, float speedMph = 0, float climbMps = 0, bool withGps = true) {
    const uint32_t started = millis();
    while (uint32_t(millis() - started) < durationMs) {
      const uint32_t now = millis();
      if (uint32_t(now - sensorAtMs) >= 50) {
        sensor.altitudeM += climbMps * uint32_t(now - sensorAtMs) / 1000.0;
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
    assert(text.find("LXLVSTART V=1 CAUSE=") != std::string::npos);
    assert(text.find("LXLVSTARTCFG SPEED_MPH=") != std::string::npos);
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
    const std::string header = "START V=1 CAUSE=MANUAL T_MS=" + std::to_string(manualAt);
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
    assert(text.find("START V=1 CAUSE=AUTO") != std::string::npos);
    assert(text.find("END V=1 CAUSE=AUTO") != std::string::npos);
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
    assert(text.find("START V=1 CAUSE=AUTO") != std::string::npos);
    assert(text.find(" V=NA ") != std::string::npos);

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
  checkModel();
  checkHistory();
  checkGpsDropouts();
  checkFirmware();
  puts(
      "Launch score, spike rejection, sensor validity, circular history, delayed GPS, IGC causes "
      "and lifecycle passed");
}
