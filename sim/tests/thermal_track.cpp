#include <Arduino.h>
#include <assert.h>
#include <stdio.h>

#include <fstream>

#include "dispatch/message_bus.h"
#include "display_capture.h"
#include "navigation/thermal_tracker.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/display.h"
#include "ui/display/pages/primary/page_thermal_core.h"
#include "ui/display/pages/primary/page_thermal_track.h"
#include "ui/settings/settings.h"

extern MessageBus<11> bus;

namespace {
  sim::Frame drawTrack() {
    thermalTrackPage_draw();
    return sim::captureFrame();
  }

  void save(const sim::Frame& frame, const char* path) {
    const auto png = sim::encodePng(frame, 4);
    std::ofstream out(path, std::ios::binary);
    out.write(png.data(), png.size());
    assert(out.good());
  }

  void checkSharedVario() {
    thermalCorePage_draw();
    const auto core = sim::captureFrame();
    const auto track = drawTrack();
    for (int y = 11; y <= 71; ++y) {
      for (int x = 0; x < 17; ++x) {
        assert(core.pixels[y * core.width + x] == track.pixels[y * track.width + x]);
      }
    }
    for (int y = 34; y < 48; ++y) {
      for (int x = 17; x < 35; ++x) {
        assert(core.pixels[y * core.width + x] == track.pixels[y * track.width + x]);
      }
    }
  }

  void checkArrows(ThermalDisplayItem& selected) {
    selected.turnDeg = 0;
    const auto straight = drawTrack();
    for (int16_t turn : {-80, -50, -30, -20, 20, 30, 50, 80}) {
      selected.turnDeg = turn;
      const auto frame = drawTrack();
      size_t changed = 0;
      for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
          const size_t p = y * frame.width + x;
          if (frame.pixels[p] == straight.pixels[p]) continue;
          assert(y >= 63 && y < 73);
          assert(turn < -20 ? (x >= 39 && x < 47) : (turn > 20 && x >= 88 && x < 96));
          ++changed;
        }
      }
      assert(abs(turn) <= 20 ? changed == 0 : changed > 0);
    }
  }
}  // namespace

int main() {
  auto& device = sim::runtime();
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/thermal-track-test/state";
  options.cardRoot = "sim/build-tests/thermal-track-test/card";
  device.configure(options);
  device.boot();
  settings.labs_thermalTrack = settings.labs_thermalCore = true;
  settings.disp_showThermalTrackPage = true;
  sim::scenario().setBus(&bus);
  std::string error;
  assert(sim::scenario().load("sim/recordings/thermal-core.json", error));
  sim::scenario().play();
  while (sim::clock().nowUs() < 350000000) device.step();
  assert(thermalTracker.savedThermalCount() > 0);
  auto* selected = const_cast<ThermalDisplayItem*>(thermalTracker.selectedDisplayItem());
  assert(selected != nullptr);
  const auto original = *selected;
  selected->distanceM = 187;
  selected->avgClimbCms = 200;
  selected->xOffset = 9;
  selected->yOffset = -13;

  for (bool imperial : {false, true}) {
    settings.units_alt = settings.units_climb = settings.units_distance = imperial;
    checkSharedVario();
    checkArrows(*selected);
    selected->turnDeg = -50;
    save(drawTrack(), imperial ? "sim/build-tests/thermal-track-imperial.png"
                               : "sim/build-tests/thermal-track-metric.png");
  }

  const auto baseline = drawTrack();
  thermalTrackPage_button(Button::DOWN, ButtonEvent::CLICKED, 0);
  const auto cursor = drawTrack();
  size_t changed = 0;
  for (int y = 0; y < cursor.height; ++y) {
    for (int x = 0; x < cursor.width; ++x) {
      const size_t p = y * cursor.width + x;
      if (cursor.pixels[p] == baseline.pixels[p]) continue;
      assert(x >= 18 && x <= 55 && y >= 11 && y <= 25);
      ++changed;
    }
  }
  assert(changed > 0);
  save(cursor, "sim/build-tests/thermal-track-alt-cursor.png");
  const auto altType = settings.disp_thmPageAltType;
  thermalTrackPage_button(Button::CENTER, ButtonEvent::CLICKED, 0);
  assert(settings.disp_thmPageAltType != altType);
  thermalTrackPage_button(Button::UP, ButtonEvent::CLICKED, 0);  // leave altitude selection

  selected->distanceM = 32767;
  selected->avgClimbCms = -32768;
  selected->turnDeg = 80;
  save(drawTrack(), "sim/build-tests/thermal-track-long-values.png");
  *selected = original;
  thermalTracker.reset();
  save(drawTrack(), "sim/build-tests/thermal-track-empty.png");
  puts("ThermalTrack layout, shared vario, steering arrows and altitude selection checks passed");
}
