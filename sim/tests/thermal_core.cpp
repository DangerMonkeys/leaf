#include <Arduino.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <array>
#include <fstream>
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
      for (int16_t cx : {-32767, -20, 2, 48, 93, 110, 32767}) {
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
            if (!(x > 0 && x < 95 && y > 73 && y < 174)) {
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

  void checkTargetDrawing() {
    auto& estimate = const_cast<ThermalCoreEstimate&>(thermalCore.estimate());
    for (int8_t direction : {-1, 1}) {
      sim::Frame neutral;
      // Advice must no longer add rings or otherwise change the displayed target.
      for (int8_t advice : {0, -127, -94, -93, -21, -20, 20, 21, 93, 94, 127}) {
        estimate = {};
        estimate.centerValid = true;
        estimate.centerX = 48;
        estimate.centerY = 132;
        estimate.valid = true;
        estimate.direction = direction;
        estimate.adviceQ7 = advice;
        // A filled breadcrumb crosses the base circle: its white inner edge must stay visible.
        estimate.markerCount = 1;
        estimate.markers[0] = {true, 74, 132, ThermalCoreMarkerGlyph::FilledRing11};
        thermalCorePage_draw();
        const auto frame = sim::captureFrame();
        const auto black = [&](int radius) {
          return frame.pixels[132 * frame.width + 48 + radius];
        };
        // 30 m is 26 pixels: the main ring is black at 26/27, edged in white at 25/28.
        assert(black(26) && black(27) && !black(25) && !black(28));
        // The crosshair is no longer interrupted by white edges of auxiliary target rings.
        assert(black(30) && black(35) && black(20) && black(15));
        assert(black(31) && black(36) && black(21) && black(16));
        if (advice == 0)
          neutral = frame;
        else
          assert(frame == neutral);
      }
    }
    estimate = {};
  }

  void checkWindArrowDrawing() {
    auto& estimate = const_cast<ThermalCoreEstimate&>(thermalCore.estimate());
    auto& wind = const_cast<WindEstimate&>(windEstimator.getWindEstimate());
    const WindEstimate savedWind = wind;
    estimate = {};
    estimate.centerValid = true;
    estimate.centerX = 48;
    estimate.centerY = 135;
    estimate.markerCount = 2;
    estimate.markers[0] = {true, 48, 135, ThermalCoreMarkerGlyph::FilledRing11};
    estimate.markers[1] = {true, 54, 137, ThermalCoreMarkerGlyph::Cross3};
    const int dx[] = {0, 1, 0, -1};
    const int dy[] = {-1, 0, 1, 0};
    for (int course = 0; course < 4; ++course) {
      estimate.mapCourseDeciDeg = course * 900;
      for (int heading = 0; heading < 4; ++heading) {
        wind.validEstimate = true;
        wind.windSpeed = 2;
        wind.windDirectionTrue = heading * 90 * DEG_TO_RAD;
        wind.windDirectionFrom = wind.windDirectionTrue + PI;
        thermalCorePage_draw();
        const auto frame = sim::captureFrame();
        const int relative = (heading - course + 4) % 4;
        const auto black = [&](int along, int across) {
          const int x = 48 + dx[relative] * along - dy[relative] * across;
          const int y = 135 + dy[relative] * along + dx[relative] * across;
          return frame.pixels[y * frame.width + x];
        };
        assert(black(4, 0) && black(5, 0));  // tip reaches the rim downwind
        assert(black(-4, -3) && black(-4, 3));
        assert(black(-3, -2) && black(-3, 2));  // wider base connects to both rim vertices
        assert(black(-1, 0) && !black(-2, 0) && !black(-3, 0));  // fluted tail notch
        assert(!black(-2, 3) && !black(0, 4));  // disc clears crosshairs and breadcrumb
        assert(black(5, 0) && black(-5, 0) && black(0, 5) && black(0, -5));  // black rim
        assert(!black(6, 0) && !black(-6, 0) && !black(0, 6) && !black(0, -6));
        assert(!frame.pixels[137 * frame.width + 54]);  // white buffer also clears diagonal crumbs
      }
    }
    estimate.mapCourseDeciDeg = 0;
    wind.windDirectionTrue = 30 * DEG_TO_RAD;
    wind.windDirectionFrom = wind.windDirectionTrue + PI;
    thermalCorePage_draw();
    const auto angledFrame = sim::captureFrame();
    const auto angledBlack = [&](int x, int y) {
      return angledFrame.pixels[(135 + y) * angledFrame.width + 48 + x];
    };
    // At a non-cardinal bearing, each snapped rim vertex still connects to the triangle fill.
    assert(angledBlack(3, -4) && angledBlack(2, -3));
    assert(angledBlack(1, 5) && angledBlack(1, 4));
    assert(angledBlack(-5, 2) && angledBlack(-4, 2));
    // Without a valid wind direction, leave the center's crosshair and breadcrumbs visible.
    for (bool invalidDirection : {false, true}) {
      wind.validEstimate = invalidDirection;
      wind.windDirectionTrue = NAN;
      estimate.mapCourseDeciDeg = 0;
      thermalCorePage_draw();
      const auto frame = sim::captureFrame();
      assert(frame.pixels[135 * frame.width + 53]);
      assert(frame.pixels[140 * frame.width + 48]);
    }
    wind = savedWind;
    estimate = {};
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
    checkTargetDrawing();
    checkWindArrowDrawing();
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

  void checkGuidanceRetention() {
    auto& device = sim::runtime();
    const auto startFlight = [&](int initialDirection, float exitRate, bool reverse) {
      sim::scenario().pause();
      thermalTracker.reset();
      thermalCore.reset();
      const char* path = "sim/build-tests/thermal-core-test/retention.json";
      {
        std::ofstream out(path);
        out << "{\"start\":{\"lat\":47.51,\"lon\":11.18,\"altitudeM\":" << device.status().altitudeM
            << "},\"legs\":["
            << "{\"durationS\":60,\"airspeedMps\":9.5,\"climbMps\":2,\"turnRateDegPerS\":"
            << initialDirection * 20 << "},"
            << "{\"durationS\":" << (reverse ? 5 : 45)
            << ",\"airspeedMps\":9.5,\"climbMps\":2,\"turnRateDegPerS\":" << exitRate << "}";
        if (reverse) {
          out << ",{\"durationS\":60,\"airspeedMps\":9.5,\"climbMps\":2,\"turnRateDegPerS\":"
              << -initialDirection * 20 << "}";
        }
        out << "]}";
        assert(out.good());
      }
      std::string error;
      assert(sim::scenario().load(path, error));
      sim::scenario().play();
      return sim::clock().millis();
    };
    const auto flyUntil = [&](uint32_t timeMs) {
      while (sim::clock().millis() < timeMs) device.step();
    };
    for (int direction : {-1, 1}) {
      for (float exitRate : {0.0f, -3.0f, 3.0f}) {
        const uint32_t start = startFlight(direction, exitRate, false);
        flyUntil(start + 60000);
        assert(thermalCore.estimate().centerValid);
        // Lift stays strong: straight flight must still time out after 15 seconds.
        flyUntil(start + 73000);
        assert(thermalCore.estimate().centerValid);
        flyUntil(start + 80000);
        assert(thermalCore.estimate().centerValid == (exitRate != 0));
        flyUntil(start + 100000);
        assert(!thermalCore.estimate().centerValid && thermalCore.estimate().direction == 0);
      }

      const uint32_t start = startFlight(direction, 0, true);
      flyUntil(start + 60000);
      assert(thermalCore.estimate().centerValid);
      // Five seconds straight, then a full turn in the opposite direction. The last good
      // center must bridge the invalid mixed-direction lap until a fresh fit is available.
      while (sim::clock().millis() < start + 110000) {
        device.step();
        assert(thermalCore.estimate().centerValid);
      }
      assert(thermalCore.estimate().direction == -direction);
    }
    // Retaining the center must not make stale GPS appear current.
    sim::scenario().pause();
    flyUntil(sim::clock().millis() + 4000);
    assert(!thermalCore.estimate().centerValid);
    thermalTracker.reset();
    thermalCore.update();
    assert(!thermalCore.estimate().centerValid && thermalCore.estimate().direction == 0);
  }
}  // namespace

int main() {
  checkLiftCenter();
  checkProjection();
  checkRecordedHistory();
  checkGuidanceRetention();
  puts(
      "Thermal projection, lift center, target rings, turn retention and recorded-history checks "
      "passed");
}
