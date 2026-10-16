#include <Arduino.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <array>
#include <vector>

#include "dispatch/message_bus.h"
#include "display_capture.h"
#include "navigation/thermal_center.h"
#include "navigation/thermal_core.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/pages/primary/page_thermal_core.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

extern MessageBus<11> bus;

namespace {
  void checkLiftCenter() {
    std::array<ThermalTracker::CoreSample, 37> circle;
    for (uint8_t i = 0; i < circle.size(); ++i) {
      auto& sample = circle[i];
      const float angle = i * 10 * DEG_TO_RAD;
      sample.valid = true;
      sample.xM = roundf(100 + 30 * sinf(angle));
      sample.yM = roundf(200 + 30 * cosf(angle));
      sample.altM = 1000;
      sample.courseDeg = (i * 10 + 90) % 360;
      sample.climbCms = 200;
      sample.capturedAtMs = 1000 + i * 1000;
    }
    const auto locate = [&](const auto& samples, float altitude = 1000, int8_t direction = 1) {
      return estimateThermalLiftCenter(samples, samples.size(), direction, altitude, 5);
    };
    auto center = locate(circle);
    assert(center.valid);
    assert(fabsf(center.eastM - 100) < 0.2f && fabsf(center.northM - 200) < 0.2f);
    assert(!estimateThermalLiftCenter(circle, 28, 1, 1000, 5).valid);  // partial turn
    assert(!locate(circle, 1000, -1).valid);                           // wrong turn direction
    auto samples = circle;
    for (uint8_t i = 0; i < samples.size(); ++i) {
      samples[i].climbCms = roundf(200 + 100 * sinf(i * 10 * DEG_TO_RAD));
    }
    center = locate(samples);
    // A known sinusoidal lift difference shifts the centroid 7.5 m toward the strong side.
    assert(center.valid && fabsf(center.eastM - 107.5f) < 0.3f);
    assert(fabsf(center.northM - 200) < 0.3f);
    for (auto& sample : samples) sample.climbCms = -100;
    assert(!locate(samples).valid);
    for (auto& sample : samples) sample.climbCms = 10;
    assert(!locate(samples).valid);  // too little lift
    samples = circle;
    samples[20].capturedAtMs += 2500;
    assert(!locate(samples).valid);  // broken GPS history
    samples = circle;
    samples[20].courseDeg = (samples[20].courseDeg + 120) % 360;
    assert(!locate(samples).valid);  // isolated course jump
    samples = circle;
    for (auto& sample : samples) sample.xM = 100 + (sample.xM - 100) / 10;
    assert(!locate(samples).valid);  // nearly a line, despite the reported heading changes
    samples = circle;
    for (auto& sample : samples) sample.wind = ThermalSampleWind::capture(true, 2, 0);
    center = locate(samples, 1100);
    assert(center.valid && fabsf(center.eastM - 100) < 0.2f);
    assert(fabsf(center.northM - (200 + 200.0f / 3)) < 0.2f);
    assert(fabsf(center.slopeNorth - 2.0f / 3) < 0.001f);
    for (auto& sample : samples) sample.courseDeg = (360 - sample.courseDeg) % 360;
    assert(locate(samples, 1100, -1).valid);  // also supports left turns
  }

  void checkCenterDrawing() {
    // Draw the real firmware page, including all neighboring fields, then compare every pixel.
    auto& estimate = const_cast<ThermalCoreEstimate&>(thermalCore.estimate());
    estimate = {};
    thermalCorePage_draw();
    const auto baseline = sim::captureFrame();
    assert(baseline.width == 96 && baseline.height == 192);
    for (int16_t course : {0, 137, 450, 900, 1799, 2700}) {
      for (int16_t cx : {-32767, -20, 48, 110, 32767}) {
        estimate.centerValid = true;
        estimate.centerX = cx;
        estimate.centerY = 124;
        estimate.mapCourseDeciDeg = course;
        thermalCorePage_draw();
        const auto frame = sim::captureFrame();
        size_t changed = 0;
        for (int y = 0; y < frame.height; ++y) {
          for (int x = 0; x < frame.width; ++x) {
            const size_t p = y * frame.width + x;
            if (frame.pixels[p] == baseline.pixels[p]) continue;
            if (!(x > 0 && x < 95 && y > 71 && y < 174)) {
              fprintf(stderr, "Crosshair escaped: course=%d cx=%d pixel=%d,%d before=%d after=%d\n",
                      course, cx, x, y, baseline.pixels[p], frame.pixels[p]);
              assert(false);
            }
            ++changed;
          }
        }
        if (cx == 48) {
          assert(changed > 40);
          const int x = course == 450 ? 70 : 48;
          const int y = course == 450 ? 102 : 100;
          if (course == 0 || course == 450) assert(frame.pixels[y * frame.width + x]);
        }
      }
    }
    estimate = {};
    thermalCorePage_draw();
    assert(sim::captureFrame() == baseline);  // no clip-window or crosshair state leaks
  }

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
    bool sawCenter = false;
    while (sim::clock().nowUs() < 180000000) {
      device.step();
      sawCenter |= thermalCore.estimate().centerValid;
      if (sim::clock().nowUs() < 90000000) assert(!thermalCore.estimate().centerValid);
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
    assert(wrapped && sawWind && sawCenter);
    checkCenterDrawing();  // GPS is now fixed, so the searching icon no longer blinks per draw.
    const auto before = thermalTracker.recentCoreSamples()[0];
    windEstimator.clearWindEstimate();
    const auto after = thermalTracker.recentCoreSamples()[0];
    assert(before.wind.east == after.wind.east && before.wind.north == after.wind.north);
    thermalTracker.reset();
    assert(thermalTracker.recentCoreSamples().size() == 0);
    thermalCore.update();
    assert(!thermalCore.estimate().centerValid);
    thermalCore.reset();
    assert(!thermalCore.estimate().centerValid);
  }
}  // namespace

int main() {
  checkLiftCenter();
  checkProjection();
  checkRecordedHistory();
  puts("Thermal projection, lift center, clipped compass lines and recorded-history checks passed");
}
