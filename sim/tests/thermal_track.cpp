#include <Arduino.h>
#include <assert.h>
#include <stdio.h>

#include <fstream>

#include "dispatch/message_bus.h"
#include "display_capture.h"
#include "instruments/baro.h"
#include "navigation/thermal_tracker.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/display.h"
#include "ui/display/pages/primary/page_thermal_core.h"
#include "ui/display/pages/primary/page_thermal_track.h"
#include "ui/display/thermal_page_fields.h"
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
    const auto track = drawTrack();
    u8g2.firstPage();
    do {
      thermal_page::drawVario(baro.climbRateFilteredValid() ? baro.climbRateFiltered() : 0);
      thermal_page::drawClimbRate(
          baro.climbRateFilteredValid() ? baro.climbRateForDisplay() : 0, true);
    } while (u8g2.nextPage());
    const auto compact = sim::captureFrame();
    for (int y = 11; y <= 71; ++y) {
      for (int x = 0; x < 17; ++x) {
        assert(compact.pixels[y * compact.width + x] == track.pixels[y * track.width + x]);
      }
    }
    for (int y = 34; y < 48; ++y) {
      for (int x = 17; x < 35; ++x) {
        assert(compact.pixels[y * compact.width + x] == track.pixels[y * track.width + x]);
      }
    }
  }

  sim::Frame drawCoreClimb(int32_t climb) {
    u8g2.firstPage();
    do {
      thermal_page::drawVario(climb, 2, thermal_page::CORE_VARIO_BAR_WIDTH);
      thermal_page::drawCoreClimbRate(climb);
    } while (u8g2.nextPage());
    return sim::captureFrame();
  }

  void checkCoreClimbLayout() {
    for (bool fpm : {false, true}) {
      settings.units_climb = fpm;
      const int right = fpm ? 54 : 43;
      for (int32_t climb : {0, 5, 45, 95, 551, 999, 1059, 6000, -551}) {
        const auto frame = drawCoreClimb(climb);
        // Fixed box size and one-pixel padding survive leading blanks, sign and saturation.
        for (int x = 14; x <= right; ++x) {
          assert(frame.pixels[35 * frame.width + x]);
          assert(frame.pixels[50 * frame.width + x]);
        }
        for (int y = 35; y <= 50; ++y) {
          assert(frame.pixels[y * frame.width + right]);
          assert(!frame.pixels[y * frame.width + right + 1]);
        }
      }
      const auto limit = drawCoreClimb(fpm ? 6000 : 990);
      const auto beyond = drawCoreClimb(fpm ? 7000 : 2000);
      for (int y = 35; y <= 50; ++y) {
        for (int x = 14; x <= right; ++x) {
          assert(limit.pixels[y * limit.width + x] == beyond.pixels[y * beyond.width + x]);
        }
      }
    }
    settings.units_climb = true;
    const auto low = drawCoreClimb(45);     // 80 fpm
    const auto mid = drawCoreClimb(551);    // 1080 fpm
    const auto high = drawCoreClimb(1059);  // 2080 fpm
    for (int y = 35; y <= 50; ++y) {
      for (int x = 32; x <= 45; ++x) {
        const int p = y * low.width + x;
        assert(low.pixels[p] == mid.pixels[p] && low.pixels[p] == high.pixels[p]);
      }
    }
    save(low, "sim/build-tests/core-climb-0080.png");
    save(mid, "sim/build-tests/core-climb-1080.png");
    settings.units_climb = false;
  }

  sim::Frame drawCoreAltitude(int32_t altitude, uint8_t& endX) {
    uint8_t tensX = 0;
    u8g2.firstPage();
    do {
      endX = thermal_page::drawCoreAltitude(altitude, 15, 30, &tensX);
    } while (u8g2.nextPage());
    assert(tensX == (abs(altitude) >= 10000 || altitude <= -1000 ? 45 : 41));
    return sim::captureFrame();
  }

  void checkCoreAltitudeLayout() {
    uint8_t endX = 0;
    const auto one = drawCoreAltitude(1, endX);
    assert(endX == 59);
    for (int32_t altitude : {11, 111, 1111, 9999}) {
      const auto frame = drawCoreAltitude(altitude, endX);
      assert(endX == 59);
      if (altitude != 9999) {
        for (int y = 13; y < 30; ++y) {
          for (int x = 50; x < 59; ++x) {
            assert(frame.pixels[y * frame.width + x] == one.pixels[y * one.width + x]);
          }
        }
      }
      bool comma = false;
      for (int y = 13; y < 30; ++y) {
        for (int x = 26; x < 29; ++x) comma |= frame.pixels[y * frame.width + x];
      }
      assert(comma == (altitude >= 1000));
    }
    for (int32_t altitude : {0, 1, 12, 123, 1234, 12345, 99999, -42, -1234}) {
      const auto frame = drawCoreAltitude(altitude, endX);
      assert(endX == 59);
      for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
          if (frame.pixels[y * frame.width + x]) {
            assert(x >= 15 && x < endX && y >= 13 && y < 30);
          }
        }
      }
    }
    for (int32_t altitude : {1234, 12345}) {
      const auto frame = drawCoreAltitude(altitude, endX);
      const int tensX = altitude < 10000 ? 41 : 45;
      const int top = altitude < 10000 ? 13 : 15;
      int highTop = 192, lowTop = 192, lowBottom = -1;
      for (int y = 0; y < frame.height; ++y) {
        for (int x = 15; x < endX; ++x) {
          if (!frame.pixels[y * frame.width + x]) continue;
          if (x < tensX) highTop = std::min(highTop, y);
          else {
            lowTop = std::min(lowTop, y);
            lowBottom = std::max(lowBottom, y);
          }
        }
      }
      assert(highTop == top && lowTop == highTop && lowBottom == 26);
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
  checkCoreClimbLayout();
  checkCoreAltitudeLayout();
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
