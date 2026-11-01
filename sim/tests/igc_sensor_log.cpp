#include "logbook/igc_sensor_log.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <map>
#include <string>
#include <vector>

struct Reader {
  struct Bounds {
    unsigned low = 0xFFFF, high = 0;
    bool committed = false, discarded = false;
  };
  std::map<unsigned, Bounds> buckets;
  std::vector<std::string> lines;
  unsigned bucket = 0;
  size_t bytes = 0;
  unsigned samples = 0, extra = 0, boundary = 0;
  static unsigned hex(const std::string& line, size_t offset, size_t length) {
    return std::stoul(line.substr(offset, length), nullptr, 16);
  }
  void add(unsigned id, unsigned g) {
    if (!id || g == 0xFFFF) return;
    auto& b = buckets[id];
    assert(!b.committed && !b.discarded);
    if (g < b.low) b.low = g;
    if (g > b.high) b.high = g;
  }
  void operator()(const char* text) {
    std::string line(text);
    lines.push_back(line);
    bytes += line.size() + 6;  // LXLV + CRLF: conservatively budget both newline bytes.
    if (line[0] == 'S') {
      assert(line.size() == 23);
      ++samples;
      bucket = hex(line, 9, 6);
      add(bucket, hex(line, 15, 4));
    } else if (line[0] == 'X') {
      assert(line.size() == 13);
      ++extra;
      add(bucket, hex(line, 9, 4));
    } else if (line[0] == 'P') {
      assert(line.size() == 19);
      ++boundary;
      add(hex(line, 9, 6), hex(line, 15, 4));
    } else {
      assert(line.size() == 7 && (line[0] == 'C' || line[0] == 'D'));
      auto& b = buckets[hex(line, 1, 6)];
      assert(!b.committed && !b.discarded);
      if (line[0] == 'C')
        b.committed = true;
      else
        b.discarded = true;
    }
  }
  void compare(const GLoadTracker& tracker) const {
    unsigned low = 0xFFFF, high = 0;
    for (const auto& item : buckets) {
      const auto& b = item.second;
      if (!b.committed) continue;
      if (b.low < low) low = b.low;
      if (b.high > high) high = b.high;
    }
    assert(tracker.valid() == (low != 0xFFFF));
    if (tracker.valid()) {
      assert(low == static_cast<unsigned>(lroundf(tracker.minG() * 1000)));
      assert(high == static_cast<unsigned>(lroundf(tracker.maxG() * 1000)));
    }
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
    if (dt >= 133000 && dt < 137000) g = 5;  // Tail must never count.
    if (dt >= 177000 && dt < 180000) g = 4;  // Relaunch guard.
    if (dt >= 210000 && dt < 213000) g = 3;
    tracker.addSample(g, now);
    // Offset log ticks from IMU and bucket boundaries to exercise the final partial interval.
    if (dt % 1000 == 650) {
      const bool gpsFix = !(dt >= 44000 && dt < 122000);
      writer.update(tracker.logState(now), now, true, -12.3f, true, gpsFix, emit);
      reader.compare(tracker);
    }
  }
  tracker.stopFlight();
  writer.update(tracker.logState(start + 290000), start + 290000, false, 0, false, false, emit);
  reader.compare(tracker);
  assert(tracker.maxG() > 2.99f && tracker.maxG() < 3.01f);
  assert(tracker.minG() < .31f);
  assert(reader.boundary > 0);
  for (const auto& item : reader.buckets) assert(item.second.committed || item.second.discarded);
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
    writer.update(state, 1600, true, -12.3f, true, true, emit);
    assert(reader.lines.back() == "S000005DC00000004D2F85B");
    state.fresh = false;
    writer.update(state, 2600, false, 0, false, true, emit);
    assert(reader.lines.back() == "S00000A28000000FFFF8000");
    writer.update(state, 3600, true, NAN, false, true, emit);
    assert(reader.lines.back().substr(19) == "8000");

    // Both new bounds must keep their original times, independent of the latest live value.
    state.current.id = 1;
    state.current.range.add(.4f, 2700);
    state.current.range.add(2.8f, 2900);
    writer.update(state, 3700, true, 18, true, true, emit);
    assert(reader.lines[reader.lines.size() - 2] == "S00000B540000010AF00B4F");
    assert(reader.lines.back() == "X00000A8C0190");
    // No repeated extrema when unchanged; explicit reset/staleness stays unavailable.
    writer.update(state, 4700, true, 18, true, true, emit);
    assert(reader.lines.back()[0] == 'S' && reader.lines.back().substr(15, 4) == "FFFF");
    writer.finish(state, emit);
    assert(reader.lines.back() == "D000001");
  }
  {
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
      state.current.range.add(1 - (second % 30 + 1) * .001f, second * 1000 + 100);
      state.current.range.add(1 + (second % 30 + 1) * .001f, second * 1000 + 300);
      state.previous = {};
      if (id > 1) {
        state.previous.id = id - 1;
        state.previous.range.add(.95f, second / 30 * 30000 - 200);
        state.previous.range.add(1.05f, second / 30 * 30000 - 100);
      }
      state.committedThrough = id > 2 ? id - 2 : 0;
      writer.update(state, second * 1000 + 500, true, 18, true, true, emit);
    }
    const size_t budget = reader.bytes + 1024;  // Configuration, clocks and final markers.
    assert(reader.samples == 3600 && reader.extra == 3600);
    assert(budget < 55 * 3600);
    printf("Worst-case sensor overhead incl. CRLF and metadata: %zu bytes/hour (<198000)\n",
           budget);
  }
  puts("Compact sensor records: eligibility, peaks, timing, recovery and size passed");
}
