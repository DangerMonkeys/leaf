#include "diagnostics/flight_diagnostics.h"

#include <assert.h>
#include <string>
#include <thread>
#include <vector>

int main() {
  using Kind = FlightDiagnostics::Kind;
  FlightDiagnostics d;
  std::vector<std::string> lines;
  auto write = [&](const char* line) {
    assert(std::string(line).size() + 4 <= 76);
    lines.emplace_back(line);
  };
  d.record(Kind::SlowWrite, 100, 900);  // Idle events do not leak into a flight.
  d.begin(1000);
  d.report(31000, write);
  assert(lines.empty());  // Healthy flight produces no periodic diagnostic lines.
  d.record(Kind::SlowWrite, 32000, 900);
  d.record(Kind::SlowWrite, 33000, 100);
  d.report(60999, write);
  assert(lines.empty());
  d.report(61000, write);
  assert(lines.size() == 1);
  assert(lines[0] == "FD K=WRITE N=2 T=32000 DT=900 R=0");
  lines.clear();
  d.record(Kind::ImuReset, 62000, 0, 8);
  d.record(Kind::ImuReset, 63000, 0, 16);
  d.report(63001, write, true);  // A short flight's final report bypasses the interval.
  assert(lines.size() == 1 && lines[0] == "FD K=IMU N=2 T=63000 DT=0 R=24");
  lines.clear();
  for (unsigned i = 0; i < 100000; ++i) d.record(Kind::MotionGap, UINT32_MAX, UINT32_MAX);
  d.record(Kind::PressureGap, 64000, 700);
  d.report(
      64001,
      [&](const char* line) {
        write(line);
        d.record(Kind::SlowWrite, 65000,
                 950);  // A slow report is deferred, never recursively emitted.
      },
      true);
  assert(lines.size() == 2);
  assert(lines[0] == "FD K=MOTION N=65535 T=4294967295 DT=4294967295 R=0");
  lines.clear();
  d.report(94001, write);
  assert(lines.size() == 1 && lines[0] == "FD K=WRITE N=2 T=65000 DT=950 R=0");
  lines.clear();
  d.record(Kind::FifoReset, 95000, 0, 2);
  d.end();
  d.begin(UINT32_MAX - 1000);
  d.report(500, write, true);
  assert(lines.empty());
  d.record(Kind::KalmanReset, 600, 1501);
  d.report(28998, write);
  assert(lines.empty());
  d.report(30500, write);
  assert(lines.size() == 1 && lines[0] == "FD K=KALMAN N=1 T=600 DT=1501 R=0");
  lines.clear();
  d.begin(0);
  auto producer = [&] {
    for (unsigned i = 0; i < 10000; ++i) d.record(Kind::ImuReset, 1, 0, 8);
  };
  std::thread first(producer), second(producer);
  first.join();
  second.join();
  d.report(30000, write);
  assert(lines.size() == 1 && lines[0] == "FD K=IMU N=20000 T=1 DT=0 R=8");
  puts("Flight event limits, timestamps, saturation, final drain and rollover passed");
}
