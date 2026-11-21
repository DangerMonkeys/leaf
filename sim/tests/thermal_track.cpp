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
#include "ui/display/fonts.h"
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
    thermalCorePage_draw();
    const auto core = sim::captureFrame();
    // The shared instruments match above Core's new history pane.
    for (int y = 0; y < 51; ++y) {
      for (int x = 0; x < track.width; ++x) {
        assert(core.pixels[y * core.width + x] == track.pixels[y * track.width + x]);
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
      const int right = 54;
      for (int32_t climb : {0, 5, 45, 95, 551, 999, 1059, 6000, -551}) {
        const auto frame = drawCoreClimb(climb);
        u8g2.firstPage();
        do {
          thermal_page::drawCoreClimbAverage(climb, 54, 56);
        } while (u8g2.nextPage());
        const auto average = sim::captureFrame();
        // Every digit slot, baseline and unit gap matches the live field, with reversed
        // colors. Exclude the sign margins: m/s now has standard 14h signs; fpm is unchanged.
        const int firstDigitColumn = fpm ? (climb < 0 ? 7 : 0) : 12;
        for (int y = 0; y < 16; ++y) {
          for (int x = firstDigitColumn; x < 41; ++x) {
            assert(frame.pixels[(35 + y) * frame.width + 14 + x] !=
                   average.pixels[(56 + y) * average.width + 54 + x]);
          }
        }
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
    const auto positive = drawCoreClimb(551);
    const auto negative = drawCoreClimb(-551);
    // The full eight-pixel 14h horizontal stroke is visible after the box is drawn,
    // including the part that moved inside it. The positive sign also has a taller stem.
    for (int y : {42, 43})
      for (int x = 11; x <= 18; ++x) {
        assert(!positive.pixels[y * positive.width + x]);
        assert(!negative.pixels[y * negative.width + x]);
      }
    assert(!positive.pixels[39 * positive.width + 14]);
    assert(negative.pixels[39 * negative.width + 14]);
    for (int y = 35; y <= 50; ++y)
      for (int x = 19; x <= 54; ++x)
        assert(positive.pixels[y * positive.width + x] == negative.pixels[y * negative.width + x]);
    save(positive, "sim/build-tests/core-climb-metric-plus.png");
    save(negative, "sim/build-tests/core-climb-metric-minus.png");
    for (bool sink : {false, true}) {
      u8g2.firstPage();
      do {
        thermal_page::drawCoreClimbAverage(sink ? -551 : 551, 54, 56);
      } while (u8g2.nextPage());
      const auto average = sim::captureFrame();
      for (int y : {63, 64})
        for (int x = 57; x <= 64; ++x) assert(average.pixels[y * average.width + x] == sink);
      save(average, sink ? "sim/build-tests/core-average-metric-minus.png"
                         : "sim/build-tests/core-average-metric-positive.png");
    }
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
          if (x < tensX)
            highTop = std::min(highTop, y);
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
          assert(y >= 61 && y < 71);
          assert(turn < -20 ? (x >= 15 && x < 23) : (turn > 20 && x >= 54 && x < 62));
          ++changed;
        }
      }
      assert(abs(turn) <= 20 ? changed == 0 : changed > 0);
    }
  }

  void checkReadoutBounds(ThermalDisplayItem& selected) {
    selected.turnDeg = 0;
    const auto baseline = drawTrack();
    for (int16_t climb : {200, 551, 1059, -32768}) {
      for (int16_t distance : {187, 304, 305, 999, 1000, 9999, 10000, 16093, 16094, 32767}) {
        selected.avgClimbCms = climb;
        selected.distanceM = distance;
        const auto frame = drawTrack();
        bool rightAligned = false;
        for (int y = 0; y < frame.height; ++y) {
          for (int x = 0; x < frame.width; ++x) {
            const size_t p = y * frame.width + x;
            if (frame.pixels[p] != baseline.pixels[p]) {
              assert(x >= 15 && x <= 95 && y >= 57 && y <= 73);
            }
          }
          if (y >= 57 && y <= 73) rightAligned |= frame.pixels[y * frame.width + 95];
        }
        assert(rightAligned);
      }
    }
    selected.avgClimbCms = 1059;
    // Four-digit fpm strength must leave the same arrow slots for every distance format.
    for (int16_t distance : {304, 999, 1000, 9999, 10000, 32767}) {
      selected.distanceM = distance;
      checkArrows(selected);
    }
  }

  void checkFixedReadoutCapacity() {
    const struct {
      const char* number;
      const char* unit;
    } distances[] = {{"999", "m"}, {"9.9", "km"}, {"9.9", "mi"}, {"99", "km"},
                     {"99", "mi"}, {"--", "km"},  {"--", "mi"}};
    for (const auto& distance : distances) {
      u8g2.setFont(leaf_6x12);
      const int16_t numberWidth = u8g2.getStrWidth(distance.number);
      u8g2.setFont(leaf_5x8);
      assert(numberWidth + 1 + u8g2.getStrWidth(distance.unit) <= 30);
    }
    u8g2.setFont(leaf_6x12);
    const int16_t digitsWidth = u8g2.getStrWidth("999");
    u8g2.setFont(leaf_6x12);
    const char feet[] = {static_cast<char>(128), '\0'};
    assert(digitsWidth + 1 + u8g2.getStrWidth(feet) <= 30);
    for (char unit : {static_cast<char>(131), static_cast<char>(132)}) {
      const char strength[] = {'9', '9', '9', '9', unit, '\0'};
      assert(u8g2.getStrWidth(strength) <= 34);
    }
  }

  void checkSelectedThermalLine(ThermalDisplayItem& selected) {
    auto* items = const_cast<ThermalDisplayItem*>(thermalTracker.displayItems());
    const uint8_t count = thermalTracker.displayItemCount();
    const std::vector<ThermalDisplayItem> originals(items, items + count);
    for (uint8_t i = 0; i < count; ++i) items[i].valid = &items[i] == &selected;
    const struct {
      int8_t x, y;
    } positions[] = {{0, -20},  {20, 0}, {0, 20},  {-20, 0}, {20, -20}, {-20, -20}, {20, 20},
                     {-20, 20}, {47, 0}, {-48, 0}, {0, -48}, {0, 47},   {0, 0},     {0, -4}};
    for (uint8_t quality = 1; quality <= 5; ++quality) {
      selected.quality = quality;
      for (const auto& position : positions) {
        selected.xOffset = position.x;
        selected.yOffset = position.y;
        selected.selected = false;
        const auto withoutLine = drawTrack();
        selected.selected = true;
        const auto withLine = drawTrack();
        const int cx = 48 + position.x, cy = 124 + position.y;
        // Every interior pixel, including hollow centers, survives in all directions and at
        // the map edge. The selection ring and line can only add pixels outside this interior.
        for (int y = std::max(0, cy - 5); y <= std::min(191, cy + 5); ++y) {
          for (int x = std::max(0, cx - 5); x <= std::min(95, cx + 5); ++x) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > 25) continue;
            const int p = y * withLine.width + x;
            assert(withLine.pixels[p] == withoutLine.pixels[p]);
          }
        }
        for (int y = 74; y < 176; ++y) {
          for (int x = 0; x < 96; ++x) {
            const int p = y * withLine.width + x;
            if (withLine.pixels[p] == withoutLine.pixels[p]) continue;
            const int dx2 = 2 * x + 1 - 96, dy2 = 2 * y + 1 - 248;
            assert(dx2 * dx2 + dy2 * dy2 <= 96 * 96);
          }
        }
      }
    }
    selected.xOffset = 0;
    selected.yOffset = -20;
    selected.quality = 1;
    const auto north = drawTrack();
    assert(north.pixels[123 * 96 + 47] && north.pixels[123 * 96 + 48]);
    assert(!north.pixels[124 * 96 + 48]);  // hollow pilot, four pixels below the old position
    for (int y = 104; y <= 107; ++y) assert(!north.pixels[y * 96 + 48]);
    assert(!north.pixels[109 * 96 + 48]);
    for (int y = 110; y <= 123; ++y) assert(north.pixels[y * 96 + 48]);
    save(north, "sim/build-tests/thermal-track-line-north.png");
    selected.xOffset = 20;
    selected.yOffset = -14;
    save(drawTrack(), "sim/build-tests/thermal-track-line-hollow.png");
    for (uint8_t i = 0; i < count; ++i) items[i] = originals[i];
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
  checkFixedReadoutCapacity();
  checkCoreClimbLayout();
  checkCoreAltitudeLayout();
  settings.disp_showThermalTrackPage = settings.disp_showThermalCorePage = true;
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
  checkSelectedThermalLine(*selected);

  for (bool imperial : {false, true}) {
    settings.units_alt = settings.units_climb = settings.units_distance = imperial;
    checkSharedVario();
    checkArrows(*selected);
    selected->turnDeg = -50;
    save(drawTrack(), imperial ? "sim/build-tests/thermal-track-imperial.png"
                               : "sim/build-tests/thermal-track-metric.png");
    checkReadoutBounds(*selected);
    selected->avgClimbCms = 1059;
    selected->distanceM = imperial ? 304 : 999;
    selected->turnDeg = 80;
    save(drawTrack(), imperial ? "sim/build-tests/thermal-track-four-digit-imperial.png"
                               : "sim/build-tests/thermal-track-three-digit-metric.png");
    selected->distanceM = imperial ? 16093 : 9999;
    save(drawTrack(), imperial ? "sim/build-tests/thermal-track-decimal-imperial.png"
                               : "sim/build-tests/thermal-track-decimal-metric.png");
    selected->distanceM = 187;
    selected->avgClimbCms = 200;
  }

  const auto baseline = drawTrack();
  thermalTrackPage_button(Button::DOWN, ButtonEvent::CLICKED, 0);
  const auto cursor = drawTrack();
  size_t changed = 0;
  for (int y = 0; y < cursor.height; ++y) {
    for (int x = 0; x < cursor.width; ++x) {
      const size_t p = y * cursor.width + x;
      if (cursor.pixels[p] == baseline.pixels[p]) continue;
      assert(x >= 15 && x <= 62 && y >= 12 && y <= 33);
      ++changed;
    }
  }
  assert(changed > 0);
  save(cursor, "sim/build-tests/thermal-track-alt-cursor.png");
  const auto altType = settings.disp_userPageAltType;
  thermalTrackPage_button(Button::CENTER, ButtonEvent::CLICKED, 0);
  assert(settings.disp_userPageAltType != altType);
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
