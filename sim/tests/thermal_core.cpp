#include <Arduino.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <vector>

#include "dispatch/message_bus.h"
#include "navigation/thermal_core.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

extern MessageBus<11> bus;

namespace {
  void expectPosition(const ThermalSampleWind& wind, float altitudeM, int16_t climbCms,
                      float expectedXM, float expectedYM) {
    float xM = 0;
    float yM = 0;
    wind.project(10, 20, 1000, climbCms, altitudeM, xM, yM);
    assert(fabsf(xM - expectedXM) < 0.001f);
    assert(fabsf(yM - expectedYM) < 0.001f);
  }

  void checkProjection() {
    const auto east = ThermalSampleWind::capture(true, 5, 1.57079632679f);
    assert(east.east == 20 && east.north == 0);
    // 3 m/s experienced climb + 1 m/s sink allowance = 4 m/s rising air.
    expectPosition(east, 1000, 300, 10, 20);
    expectPosition(east, 1040, 300, 60, 20);
    expectPosition(east, 960, 300, -40, 20);
    expectPosition(ThermalSampleWind::capture(true, 5, 0), 1040, 300, 10, 70);
    expectPosition(ThermalSampleWind::capture(true, 5, -1.57079632679f), 1040, 300, -40, 20);
    expectPosition(east, 1040, 0, 210, 20);  // minimum supported upward air speed
    for (int16_t climb : {-1, -100, -500, INT16_MIN}) {
      expectPosition(east, 1040, climb, 10, 20);
    }
    expectPosition({}, 1040, 300, 10, 20);
    expectPosition(ThermalSampleWind::capture(false, 5, 0), 1040, 300, 10, 20);
    expectPosition(ThermalSampleWind::capture(true, NAN, 0), 1040, 300, 10, 20);
    expectPosition(ThermalSampleWind::capture(true, 5, NAN), 1040, 300, 10, 20);
    expectPosition(ThermalSampleWind::capture(true, 21, 0), 1040, 300, 10, 20);
    expectPosition(ThermalSampleWind::capture(true, -1, 0), 1040, 300, 10, 20);
    expectPosition(ThermalSampleWind::capture(true, 0, 0), 1040, 300, 10, 20);
    expectPosition(east, NAN, 300, 10, 20);

    const auto rounded = ThermalSampleWind::capture(true, 5.12f, 0);
    assert(fabsf(rounded.north * 0.25f - 5.12f) <= 0.125f);
    // Each snapshot keeps its own wind even after a different wind is captured.
    const auto changed = ThermalSampleWind::capture(true, 10, 0);
    expectPosition(changed, 1040, 300, 10, 120);
    expectPosition(east, 1040, 300, 60, 20);
  }

  void checkRecordedHistory() {
    auto& device = sim::runtime();
    sim::Runtime::Options options;
    options.httpPort = 0;
    options.speed = 0;
    options.acceptWarning = true;
    options.exitOnFatalError = true;
    options.statePath = "sim/build-tests/thermal-core-test/state";
    options.cardRoot = "sim/build-tests/thermal-core-test/sdcard";
    device.configure(options);
    device.boot();
    settings.labs_thermalCore = true;
    settings.disp_showThermalCorePage = true;
    sim::scenario().setBus(&bus);
    std::string error;
    assert(sim::scenario().load("sim/recordings/thermal-core.json", error));
    sim::scenario().play();

    std::vector<ThermalTracker::CoreSample> history;
    bool sawWind = false;
    bool wrapped = false;
    while (sim::clock().nowUs() < 180000000) {
      device.step();
      const auto samples = thermalTracker.recentCoreSamples();
      if (!samples.size()) {
        history.clear();
        continue;
      }
      if (samples.size() < std::min<size_t>(history.size(), ThermalTracker::MAX_DETECTOR_SAMPLES))
        history.clear();
      const auto latest = samples[samples.size() - 1];
      if (!history.empty() && latest.capturedAtMs == history.back().capturedAtMs) continue;
      history.push_back(latest);
      sawWind |= latest.wind.east != INT8_MIN;
      const size_t count = std::min<size_t>(history.size(), ThermalTracker::MAX_DETECTOR_SAMPLES);
      assert(samples.size() == count);
      wrapped |= history.size() > ThermalTracker::MAX_DETECTOR_SAMPLES * 2;
      for (uint8_t i = 0; i < samples.size(); ++i) {
        const auto actual = samples[i];
        const auto& expected = history[history.size() - count + i];
        assert(actual.valid && actual.capturedAtMs == expected.capturedAtMs);
        assert(actual.xM == expected.xM && actual.yM == expected.yM);
        assert(actual.altM == expected.altM && actual.climbCms == expected.climbCms);
        assert(actual.wind.east == expected.wind.east && actual.wind.north == expected.wind.north);
      }
    }
    if (!wrapped || !sawWind) {
      const auto status = device.status();
      fprintf(stderr, "History check: %zu samples, wrapped=%d wind=%d labs=%d timer=%d\n",
              history.size(), wrapped, sawWind, settings.labs_thermalCore,
              status.flightTimerRunning);
    }
    assert(wrapped && sawWind);
    const auto before = thermalTracker.recentCoreSamples()[0];
    windEstimator.clearWindEstimate();
    const auto after = thermalTracker.recentCoreSamples()[0];
    assert(before.wind.east == after.wind.east && before.wind.north == after.wind.north);
    thermalTracker.reset();
    assert(thermalTracker.recentCoreSamples().size() == 0);
  }
}  // namespace

int main() {
  checkProjection();
  checkRecordedHistory();
  puts("Thermal projection and recorded-history checks passed");
}
