#include "logbook/igc_sensor_log.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <string>
#include <vector>

struct Reader {
  struct Point {
    uint32_t ms;
    unsigned g, temperature;
  };
  std::vector<Point> points;
  std::vector<std::string> lines;
  size_t bytes = 0;
  unsigned samples = 0;
  static unsigned hex(const std::string& line, size_t offset, size_t length) {
    return std::stoul(line.substr(offset, length), nullptr, 16);
  }
  void operator()(const char* text) {
    std::string line(text);
    lines.push_back(line);
    bytes += line.size() + 6;  // LXLV + CRLF: conservatively budget both newline bytes.
    assert(line[0] == 'S' && line.size() == 16);
    ++samples;
    points.push_back({hex(line, 1, 8), hex(line, 9, 4), hex(line, 13, 3)});
  }
  bool has(unsigned g, uint32_t origin, uint32_t fromMs, uint32_t toMs) const {
    for (const auto& p : points)
      if (p.g == g && uint32_t(p.ms - origin) >= fromMs && uint32_t(p.ms - origin) < toMs)
        return true;
    return false;
  }
};

static void play(uint32_t start = 1000) {
  GLoadTracker tracker;
  IgcSensorLog writer;
  Reader reader;
  auto emit = [&](const char* text) { reader(text); };
  tracker.startFlight();
  for (uint32_t dt = 0; dt < 290000; dt += 50) {
    const uint32_t now = start + dt;
    // Launch, spiral, unloaded dip, landing knock, then a second departure and maneuver.
    const bool flying = dt < 135000 || dt >= 175000;
    if (dt % 1000 == 0) tracker.updateFlightState(flying, flying);
    float g = 1;
    if (dt < 6000) g = 4;  // Launch guard.
    if (dt >= 31000 && dt < 33000) g = 2.5f;
    if (dt >= 33300 && dt < 34700) g = .3f;
    if (dt >= 74900 && dt < 75400) g = 2;    // Peak in the final, not-yet-logged bucket interval.
    if (dt >= 133000 && dt < 137000) g = 5;  // Retained in IGC, excluded by device holdback.
    if (dt >= 177000 && dt < 180000) g = 4;  // Relaunch guard.
    if (dt >= 210000 && dt < 213000) g = 3;
    tracker.addSample(g, now);
    // Offset log ticks from IMU and bucket boundaries to exercise the final partial interval.
    if (dt % 1000 == 650) {
      const bool gpsFix = !(dt >= 44000 && dt < 122000);
      writer.update(tracker.logState(now), now, true, -12.3f, gpsFix, emit);
    }
  }
  tracker.stopFlight();
  writer.update(tracker.logState(start + 290000), start + 290000, false, 0, false, emit);
  assert(tracker.maxG() > 2.99f && tracker.maxG() < 3.01f);
  assert(tracker.minG() < .31f);
  assert(reader.samples > 200);
  // The IGC retains handling/landing data for the website to classify independently.
  assert(reader.has(4000, start, 1000, 6000));
  assert(reader.has(2500, start, 31000, 35000));
  assert(reader.has(300, start, 33000, 37000));
  assert(reader.has(5000, start, 133000, 139000));
  // An earlier landing peak must not hide the next flight's smaller peak.
  assert(reader.has(3000, start, 210000, 214000));
  bool outagePeak = false;
  for (const auto& p : reader.points)
    outagePeak |= p.g > 1400 && p.g < 2500 && p.temperature == 0x800 &&
                  uint32_t(p.ms - start) >= 74900 && uint32_t(p.ms - start) < 77000;
  assert(outagePeak);
}

int main() {
  play();
  play(UINT32_MAX - 110000);  // Sensor times and acceptance survive millis() rollover.
  {
    IgcSensorLog writer;
    Reader reader;
    auto emit = [&](const char* text) { reader(text); };
    GLoadTracker::LogState state;
    state.fresh = true;
    state.filteredG = 1.234f;
    state.sampleMs = 1500;
    writer.update(state, 1600, true, -12.3f, true, emit);
    assert(reader.lines.back() == "S000005DC04D2F85");
    state.fresh = false;
    writer.update(state, 2600, false, 0, true, emit);
    assert(reader.lines.back() == "S00000A28FFFF800");
    writer.update(state, 3600, true, NAN, true, emit);
    assert(reader.lines.back().substr(13) == "800");

    // Both new bounds must keep their original times, independent of the latest live value.
    state.current.id = 1;
    state.current.range.add(.4f, 2700);
    state.current.range.add(2.8f, 2900);
    writer.update(state, 3700, true, 18, true, emit);
    assert(reader.lines[reader.lines.size() - 2] == "S00000B540AF00B4");
    assert(reader.lines.back() == "S00000A8C01900B4");
    // No repeated extrema when unchanged; explicit reset/staleness stays unavailable.
    writer.update(state, 4700, true, 18, true, emit);
    assert(reader.lines.back()[0] == 'S' && reader.lines.back().substr(9, 4) == "FFFF");
    const auto count = reader.lines.size();
    state.current = {};
    writer.update(state, 4800, false, 0, false, emit);
    assert(reader.lines.size() == count);  // No commit/discard or other terminal records.
  }
  for (bool busy : {false, true}) {
    // One-hour adversarial budget: both bounds change every second, plus two unsent
    // boundary peaks per rollover. This is substantially busier than normal flight.
    IgcSensorLog writer;
    Reader reader;
    auto emit = [&](const char* text) { reader(text); };
    GLoadTracker::LogState state;
    for (unsigned second = 0; second < 3600; ++second) {
      const unsigned id = second / 30 + 1;
      state.current.id = id;
      state.current.range = {};
      state.current.range.add(busy ? 1 - (second % 30 + 1) * .001f : 1, second * 1000 + 100);
      state.current.range.add(busy ? 1 + (second % 30 + 1) * .001f : 1, second * 1000 + 300);
      state.previous = {};
      if (id > 1) {
        state.previous.id = id - 1;
        state.previous.range.add(busy ? .95f : 1, second / 30 * 30000 - 200);
        state.previous.range.add(busy ? 1.05f : 1, second / 30 * 30000 - 100);
      }
      writer.update(state, second * 1000 + 500, true, 18, true, emit);
    }
    const size_t budget = reader.bytes + 1024;  // Configuration, clocks and final markers.
    assert(busy ? reader.samples > 7200 : reader.samples == 3600);
    assert(budget < 55 * 3600);
    printf("%s sensor overhead incl. CRLF and metadata: %zu bytes/hour\n",
           busy ? "Stress-case" : "Steady-flight", budget);
  }
  puts("Compact sensor records: unclassified peaks, timing, recovery and size passed");
}
