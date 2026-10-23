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
  void checkWidgetHistory() {
    ThermalClimbHistory history;
    assert(sizeof(history) == 100);
    // Fractional starting meters must not masquerade as climb in the first partial bucket.
    history.update(true, 1000, 100049);
    history.update(true, 1500, 100149);
    assert(history.averageCms() == 200);
    for (uint32_t ms = 2000; ms <= 251000; ms += 500) {
      history.update(true, ms, 100049 + (ms - 1000) / 5);
    }
    assert(history.size() == 40 && history.averageCms() == 200);
    assert(history.altitudeM(39) == 1110);  // ring wrapped; 195 seconds retained
    history.update(true, 251500, 150149);
    assert(history.altitudeAgoCm(1) == 149100);  // five seconds before the live reference
    for (uint32_t ms = 252000; ms <= 311000; ms += 500) history.update(true, ms, 150049);
    assert(history.averageCms() == 0);  // trailing minute forgets the earlier strong climb
    history.update(false, 311500, 150049);
    assert(!history.size() && history.averageCms() == 0);
    history.update(true, 312000, -100049);
    history.update(true, 317000, -101049);
    assert(history.size() == 2 && history.averageCms() == -200);
    history.reset();
    const uint32_t start = UINT32_MAX - 2000;
    history.update(true, start, 100000);
    history.update(true, start + 5500, 101100);
    assert(history.size() == 2 && history.averageCms() == 200);
    // Interpolation handles a late update without lengthening the five-second buckets.
    assert(history.altitudeM(0) == 1010);
  }

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
    assert(center.lapStartMs == 1000 && center.lapEndMs == 37000);
    assert(fabsf(center.climbIntegralCmsMs / center.lapDurationMs - 200) < 0.01f);
    assert(!estimateThermalLiftCenter(circle, circle.size(), 1, 1000, 5, 2000).valid);
    assert(fabsf(center.eastM - 100) < 0.2f && fabsf(center.northM - 200) < 0.2f);
    assert(!estimateThermalLiftCenter(circle, 28, 1, 1000, 5).valid);  // partial turn
    assert(!locate(circle, 1000, -1).valid);                           // wrong turn direction
    auto samples = circle;
    uint32_t timeMs = 1000;
    for (uint8_t i = 0; i < samples.size(); ++i) {
      if (i) timeMs += i <= 18 ? 500 : 1500;
      samples[i].capturedAtMs = timeMs;
      samples[i].climbCms = i <= 18 ? 300 : -100;
    }
    center = locate(samples);
    assert(center.valid);
    // 9 seconds at +3 m/s, then 27 seconds including a transition to -1 m/s.
    // Signed, time-weighted averaging differs substantially from angle/lift weighting.
    assert(fabsf(center.lapDurationMs - 36000) < 0.01f);
    assert(fabsf(center.climbIntegralCmsMs / center.lapDurationMs - 8.333333f) < 0.01f);
    samples = circle;
    for (uint8_t i = 0; i < samples.size(); ++i) {
      samples[i].courseDeg = (i * 11 + 90) % 360;
    }
    center = locate(samples);
    assert(center.valid && fabsf(center.lapDurationMs - 360000.0f / 11) < 0.01f);
    assert(center.lapStartMs == 4273);
    samples = circle;
    for (uint8_t i = 0; i < samples.size(); ++i) {
      samples[i].climbCms = roundf(200 + 100 * sinf(i * 10 * DEG_TO_RAD));
    }
    center = locate(samples);
    // The centroid alone shifts only 7.5 m. Coherent lopsided lift now guides to the strong edge.
    assert(center.valid && fabsf(center.eastM - 130) < 0.4f && center.guidanceGain > 3.8f);
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

  void checkGuidanceGain() {
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
    const auto locate = [&](const auto& samples, float altitude = 1000, int8_t direction = 1,
                            uint32_t segmentStartMs = 0) {
      return estimateThermalLiftCenter(samples, samples.size(), direction, altitude, 5,
                                       segmentStartMs);
    };
    auto center = locate(circle);
    assert(center.valid && center.guidanceGain == 1);
    assert(fabsf(center.eastM - 100) < 0.2f && fabsf(center.northM - 200) < 0.2f);
    for (int phase : {0, 90, 180, 270}) {
      auto samples = circle;
      for (uint8_t i = 0; i < samples.size(); ++i) {
        samples[i].climbCms = roundf(200 + 100 * cosf((i * 10 - phase) * DEG_TO_RAD));
      }
      center = locate(samples);
      assert(center.valid && center.guidanceGain > 3.8f);
      assert(fabsf(center.eastM - (100 + 30 * sinf(phase * DEG_TO_RAD))) < 0.4f);
      assert(fabsf(center.northM - (200 + 30 * cosf(phase * DEG_TO_RAD))) < 0.4f);
      for (auto& sample : samples) sample.courseDeg = (360 - sample.courseDeg) % 360;
      const auto left = locate(samples, 1000, -1);
      assert(left.valid && fabsf(left.eastM - center.eastM) < 0.01f);
      assert(fabsf(left.northM - center.northM) < 0.01f);
    }
    auto samples = circle;
    for (uint8_t i = 0; i < samples.size(); ++i) {
      samples[i].climbCms = roundf(200 + 10 * sinf(i * 10 * DEG_TO_RAD));
    }
    center = locate(samples);
    assert(center.valid && center.guidanceGain == 1);  // below 0.3 m/s contrast deadband
    for (uint8_t i = 0; i < samples.size(); ++i) {
      samples[i].climbCms = roundf(200 + 25 * sinf(i * 10 * DEG_TO_RAD));
    }
    center = locate(samples);
    assert(center.valid && center.guidanceGain > 1.5f && center.guidanceGain < 2.2f);
    samples = circle;
    samples[9].climbCms = 1000;
    center = locate(samples);
    assert(center.valid && center.guidanceGain == 1);  // a single hot breadcrumb gets no boost
    samples = circle;
    for (uint8_t i = 0; i < samples.size(); ++i) samples[i].climbCms = i % 2 ? 100 : 300;
    center = locate(samples);
    assert(center.valid && center.guidanceGain == 1);  // scattered noise is not a coherent slope
    samples = circle;
    for (uint8_t i = 0; i < samples.size(); ++i) {
      samples[i].climbCms = roundf(200 + 190 * sinf(i * 10 * DEG_TO_RAD));
    }
    center = locate(samples);
    assert(center.valid && center.eastM > 129 &&
           center.eastM <= 130.4f);  // bounded at sampled edge
    for (uint8_t i = 0; i < samples.size(); ++i) {
      const float angle = i * 10 + 8 * sinf(i * 20 * DEG_TO_RAD);
      samples[i].xM = roundf(100 + 45 * sinf(angle * DEG_TO_RAD));
      samples[i].yM = roundf(200 + 20 * cosf(angle * DEG_TO_RAD));
      samples[i].courseDeg = static_cast<int16_t>(roundf(angle + 90)) % 360;
      samples[i].climbCms = roundf(200 + (samples[i].xM - 100) * 100.0f / 45);
    }
    center = locate(samples);
    assert(center.valid && center.eastM > 142 && center.eastM < 146);
    assert(fabsf(center.northM - 200) <
           1);  // uneven speed/oval path retains the strong-side direction

    samples = circle;
    for (uint8_t i = 0; i < samples.size(); ++i) {
      samples[i].climbCms = 200 + 50 * roundf(2 * sinf(i * 10 * DEG_TO_RAD));
      samples[i].wind.east = (samples[i].climbCms + 100) / 50;
      samples[i].wind.north = 0;  // every observation has the same 0.5 m/m eastward slant
    }
    const auto below = locate(samples);
    const auto above = locate(samples, 1100);
    assert(below.valid && above.valid && below.guidanceGain > 3);
    assert(fabsf(above.eastM - below.eastM - 50) < 0.01f);
    assert(fabsf(above.northM - below.northM) < 0.01f && fabsf(above.slopeEast - 0.5f) < 0.001f);

    // A recent outer lap surrounds the inner lap's strongest region with weaker observations.
    // The peak is bracketed, so the inner estimate should not be extrapolated to its edge.
    for (uint8_t i = 0; i < samples.size(); ++i) {
      const float angle = i * 20 * DEG_TO_RAD;
      const float radius = i < 18 ? 40 : 20;
      samples[i] = circle[i];
      samples[i].xM = roundf(100 + radius * cosf(angle));
      samples[i].yM = roundf(200 + radius * sinf(angle));
      samples[i].courseDeg = (i * 20 + 90) % 360;
      const float x = samples[i].xM - 110, y = samples[i].yM - 200;
      samples[i].climbCms = roundf(50 + 300 * expf(-(x * x + y * y) / 450));
    }
    center = locate(samples);
    assert(center.valid && center.guidanceGain == 1 && center.eastM < 110);
    const auto unbracketed = locate(samples, 1000, 1, samples[18].capturedAtMs);
    assert(unbracketed.valid && unbracketed.guidanceGain > 3);
    assert(unbracketed.eastM >
           center.eastM + 8);  // old segments cannot supply surrounding evidence
  }

  ThermalLiftCenter targetLap(float eastM, float climbCms, uint32_t startMs, uint32_t endMs) {
    ThermalLiftCenter lap;
    lap.valid = true;
    lap.eastM = eastM;
    lap.lapStartMs = startMs;
    lap.lapEndMs = endMs;
    lap.lapDurationMs = endMs - startMs;
    lap.climbIntegralCmsMs = climbCms * lap.lapDurationMs;
    return lap;
  }

  ThermalCoreTarget establishedTarget(float climbCms = 200) {
    ThermalCoreTarget target;
    target.projectToAltitude(1000);
    target.observe(targetLap(0, climbCms, 1000, 19000), 19000);
    target.updateVisibility(0, 0, 19000, true);
    return target;
  }

  void checkClimbHistory() {
    ThermalCoreClimbHistory history;
    history.add(targetLap(0, 100, 1000, 19000));
    history.add(targetLap(0, 900, 2000, 20000));  // overlapping sliding fit is not another turn
    assert(fabsf(history.averageCms() - 100) < 0.01f);
    history.add(targetLap(0, 300, 19000, 28000));
    assert(fabsf(history.averageCms() - 166.66667f) < 0.01f);
    history.add(targetLap(0, 500, 28000, 46000));
    assert(fabsf(history.averageCms() - 433.33333f) < 0.01f);  // oldest turn discarded
    history.add(targetLap(0, -100, 46000, 64000));
    assert(fabsf(history.averageCms() - 200) < 0.01f);
  }

  void checkDistanceRetention() {
    auto target = establishedTarget();
    // Incomplete fits and straight flight nearby have no timeout, including exactly 100 m.
    for (uint32_t ms = 20000; ms <= 80000; ms += 1000) {
      target.observe({}, ms);
      target.updateVisibility(100, 0, ms, true);
      assert(target.visible() && target.averageClimbCms() == 200);
    }
    for (uint32_t ms = 81000; ms < 86000; ms += 1000) {
      target.updateVisibility(100.01f, 0, ms, true);
      assert(target.visible());
    }
    target.updateVisibility(100.01f, 0, 86000, true);
    assert(!target.visible() && target.hasCenter());
    target.updateVisibility(100, 0, 87000, true);
    assert(target.visible());
    for (uint32_t ms = 88000; ms <= 92000; ms += 1000) target.updateVisibility(101, 0, ms, true);
    target.updateVisibility(99, 0, 93000, true);  // interrupts the departure timer
    for (uint32_t ms = 94000; ms <= 98000; ms += 1000) {
      target.updateVisibility(101, 0, ms, true);
      assert(target.visible());
    }
    target.updateVisibility(101, 0, 99000, true);
    assert(!target.visible());
    target.updateVisibility(0, 0, 100000, true);
    target.updateVisibility(101, 0, 101000, true);
    target.updateVisibility(101, 0, 102000, true);
    target.updateVisibility(101, 0, 105000, false);  // GPS outage cannot count as continuous exit
    for (uint32_t ms = 108000; ms <= 112000; ms += 1000) {
      target.updateVisibility(101, 0, ms, true);
      assert(target.visible());
    }
    target.updateVisibility(101, 0, 113000, true);
    assert(!target.visible());
    target.updateVisibility(0, 0, 114000, true);
    assert(target.visible());

    ThermalCoreTarget projected;
    projected.projectToAltitude(1000);
    auto lap = targetLap(0, 200, 1000, 19000);
    lap.slopeEast = 2;
    projected.observe(lap, 19000);
    projected.projectToAltitude(1050);
    assert(projected.eastM() == 100);
    projected.updateVisibility(200, 0, 20000, true);
    assert(projected.visible());  // distance is measured at today's altitude
    projected.reset();
    assert(!projected.hasCenter() && !projected.visible());
  }

  void checkCenterReplacement() {
    auto target = establishedTarget();
    target.observe(targetLap(40, 300, 20000, 38000), 38000);
    assert(fabsf(target.eastM() - 14) < 0.01f);  // exactly 40 m still smooths
    const float once = target.eastM();
    target.observe(targetLap(40, 300, 20000, 38000), 38000);
    assert(target.eastM() == once);  // repeated display updates do not smooth twice

    target = establishedTarget();
    for (uint32_t ms = 38000; ms <= 41000; ms += 1000) {
      target.observe(targetLap(80, 300, ms - 18000, ms), ms);
      if (ms < 41000) {
        assert(target.eastM() == 0 && target.averageClimbCms() == 200);
      }
    }
    assert(target.eastM() == 80 && target.averageClimbCms() == 300);
    for (float candidateClimb : {100.0f, 200.0f}) {
      target = establishedTarget();
      target.observe(targetLap(80, candidateClimb, 20000, 38000), 38000);
      assert(fabsf(target.eastM() - 28) < 0.01f);  // weaker and equal candidates smooth immediately
      target.observe(targetLap(80, candidateClimb, 21000, 39000), 39000);
      assert(fabsf(target.eastM() - 46.2f) < 0.01f);
    }

    target = establishedTarget();
    target.observe(targetLap(80, 300, 20000, 38000), 38000);
    for (uint32_t ms = 39000; ms <= 42000; ms += 1000) {
      target.observe(targetLap(100, 300, ms - 18000, ms), ms);  // moved >15 m; restart confirmation
      if (ms < 42000) assert(target.eastM() == 0);
    }
    assert(target.eastM() == 100);
    target = establishedTarget();
    target.observe(targetLap(80, 300, 20000, 38000), 38000);
    target.observe({}, 39000);  // invalid fit interrupts confirmation without losing the old center
    for (uint32_t ms = 40000; ms <= 43000; ms += 1000) {
      target.observe(targetLap(80, 300, ms - 18000, ms), ms);
      if (ms < 43000) assert(target.eastM() == 0 && target.visible());
    }
    assert(target.eastM() == 80);

    target = establishedTarget();
    for (uint32_t ms = 20000; ms <= 25000; ms += 1000) target.updateVisibility(200, 0, ms, true);
    assert(!target.visible());
    for (uint32_t ms = 38000; ms <= 41000; ms += 1000) {
      target.observe(targetLap(200, 300, ms - 18000, ms), ms);
      target.updateVisibility(200, 0, ms, true);
    }
    assert(target.eastM() == 200 &&
           target.visible());  // hidden old target replaced without a blink
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
            const bool widget = x >= 14 && x < 95 && y >= 51 && y < 73;
            if (!(x > 0 && x < 95 && y > 73 && y < 174) && !widget) {
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

  void checkWidgetDrawing() {
    auto& estimate = const_cast<ThermalCoreEstimate&>(thermalCore.estimate());
    auto& history = const_cast<ThermalClimbHistory&>(thermalCore.climbHistory());
    const auto savedHistory = history;
    const bool savedUnits = settings.units_climb;
    history.reset();
    estimate = {};
    const auto draw = [&]() {
      thermalCorePage_draw();
      return sim::captureFrame();
    };
    const auto save = [&](const sim::Frame& frame, const char* path) {
      const auto png = sim::encodePng(frame, 4);
      std::ofstream out(path, std::ios::binary);
      out.write(png.data(), png.size());
      assert(out.good());
    };
    const auto empty = draw();
    const auto black = [&](int x, int y) { return empty.pixels[y * empty.width + x]; };
    for (int x = 14; x < 54; ++x) assert(!black(x, 51) && black(x, 73));
    for (int x = 54; x < 96; ++x) assert(black(x, 55) && black(x, 73));
    for (int y = 51; y < 73; ++y) {
      assert(!black(14, y));
      assert(black(54, y));
      for (int x = 15; x < 54; ++x) assert(!black(x, y));
    }
    save(empty, "sim/build-tests/climb-history-empty.png");
    int32_t altitudeCm = 100000;
    for (uint32_t ms = 0; ms <= 195000; ms += 500) {
      // Strong climb first, tapering to flat over the last minute.
      if (ms) altitudeCm += ms <= 120000 ? 150 : (195000 - ms) / 500;
      history.update(true, ms, altitudeCm);
    }
    estimate.centerValid = true;
    estimate.centerX = 48;
    estimate.centerY = 124;
    for (bool imperial : {false, true}) {
      settings.units_climb = imperial;
      const auto frame = draw();
      assert(frame.pixels[55 * frame.width + 53]);  // present altitude sits just left of divider
      assert(!frame.pixels[54 * frame.width + 53]);
      int filledColumns = 0;
      for (int x = 15; x < 54; ++x) {
        bool filled = false;
        for (int y = 51; y < 73; ++y) {
          if (frame.pixels[y * frame.width + x]) filled = true;
          if (filled) assert(frame.pixels[y * frame.width + x]);  // no holes below the curve
        }
        if (filled) ++filledColumns;
      }
      assert(filledColumns > 20);  // older high-gain history may be clipped below the graph
      save(frame, imperial ? "sim/build-tests/climb-history-fpm.png"
                           : "sim/build-tests/climb-history-metric.png");
    }
    // Losing the circle must hide the contents even if a stale history was accidentally retained.
    estimate = {};
    settings.units_climb = savedUnits;
    assert(draw() == empty);
    history = savedHistory;
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
      fprintf(stderr, "History check: %zu samples, wrapped=%d wind=%d core=%d timer=%d\n",
              history.size(), wrapped, sawWind, settings.disp_showThermalCorePage,
              status.flightTimerRunning);
    }
    assert(wrapped && sawWind && sawCenter);
    checkCenterDrawing();  // GPS is now fixed, so the searching icon no longer blinks per draw.
    checkTargetDrawing();
    checkWindArrowDrawing();
    checkWidgetDrawing();
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
    const auto startFlight = [&](int initialDirection, float exitRate, bool reverse,
                                 float exitSpeed = 1.0f, bool returnToCenter = false) {
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
            << "{\"durationS\":"
            << (reverse          ? 5
                : returnToCenter ? 20
                                 : 45)
            << ",\"airspeedMps\":" << exitSpeed
            << ",\"climbMps\":2,\"turnRateDegPerS\":" << exitRate << "}";
        if (returnToCenter) {
          out << ",{\"durationS\":20,\"airspeedMps\":9.5,\"climbMps\":2,\"headingDeg\":"
              << (initialDirection * 1200 + 180 + 3600) % 360 << "}";
        }
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
        // Slow excursions stay nearby even after the old straight/turning timers would expire.
        flyUntil(start + 73000);
        assert(thermalCore.estimate().centerValid);
        flyUntil(start + 80000);
        assert(thermalCore.estimate().centerValid);
        flyUntil(start + 100000);
        assert(thermalCore.estimate().centerValid);
      }

      uint32_t start = startFlight(direction, 0, false, 9.5f, true);
      flyUntil(start + 80000);
      assert(!thermalCore.estimate().centerValid);  // >100 m away long enough to hide
      flyUntil(start + 100000);
      assert(thermalCore.estimate().centerValid);  // return restores without any new turn

      start = startFlight(direction, 0, true);
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
    assert(!thermalCore.climbHistory().size());
    sim::scenario().play();
    flyUntil(sim::clock().millis() + 3000);
    assert(thermalCore.estimate().centerValid);  // GPS recovery restores the cached nearby center
    assert(thermalCore.climbHistory().size() > 0 && thermalCore.climbHistory().size() <= 2);
    thermalTracker.reset();
    thermalCore.update();
    assert(!thermalCore.estimate().centerValid && thermalCore.estimate().direction == 0);
  }
}  // namespace

int main() {
  checkWidgetHistory();
  checkLiftCenter();
  checkGuidanceGain();
  checkClimbHistory();
  checkDistanceRetention();
  checkCenterReplacement();
  checkProjection();
  checkRecordedHistory();
  checkGuidanceRetention();
  puts(
      "Thermal projection, lap climb averages, target replacement, distance retention and display "
      "checks "
      "passed");
}
