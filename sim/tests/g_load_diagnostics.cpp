#include "logbook/g_load_diagnostics.h"

#include <assert.h>
#include <stdio.h>

#include <string>
#include <vector>

static std::vector<std::string> records(const GLoadDiagnostics& d,
                                        const GLoadTracker::DiagnosticState& s) {
  std::vector<std::string> lines;
  writeGLoadDiagnostics(d, s, [&](const char* line) { lines.emplace_back(line); });
  assert(lines.size() == 3);
  for (const auto& line : lines) assert(line.size() + 4 <= 76);
  return lines;
}

int main() {
  GLoadTracker tracker;
  GLoadDiagnostics d;
  d.checkMs = 1000;
  d.rawSampleMs = 950;
  d.rawG = 1.82f;
  d.rawValid = true;
  auto cold = records(d, tracker.diagnosticState());
  assert(cold[0] == "GL T=1000 S=NA C=0 SV=0 CV=0 FS=0 VC=0 L=0");
  assert(cold[1] == "GA T=1000 RT=950 FT=0 R=1.820 F=NA RV=1 FV=0");

  tracker.startFlight();
  tracker.updateAirborne(true);
  assert(tracker.diagnosticState().ignoreRemainingMs == 15000);
  uint32_t t = 1000;
  for (; t < 2000; t += 50) tracker.addSample(1.0f, t);
  assert(tracker.diagnosticState().filteredReady);
  assert(tracker.diagnosticState().ignoreRemainingMs > 0);
  for (; t < 90000; t += 50) tracker.addSample(1.0f, t);
  assert(tracker.valid());
  assert(tracker.diagnosticState().pending);
  assert(tracker.diagnosticState().ignoreRemainingMs == 0);
  for (int i = 0; i < 4; ++i) tracker.updateAirborne(false);
  assert(tracker.diagnosticState().airborne);
  assert(tracker.diagnosticState().groundSeconds == 4);
  tracker.updateAirborne(false);
  auto landed = tracker.diagnosticState();
  assert(!landed.airborne && !landed.pending && landed.committed);
  assert(landed.bucketStartMs == 0);
  tracker.updateAirborne(true);
  assert(tracker.diagnosticState().ignoreRemainingMs == 15000);
  tracker.addSample(1.0f, t + 1000);
  assert(!tracker.diagnosticState().filteredReady);  // Stale filter history after a gap.

  // Numeric rounding must not override the actual threshold result.
  d.speedValid = true;
  d.speedMps = 3.5764f;
  d.fast = true;
  d.climbValid = true;
  d.climbCms = -50;
  d.vertical = false;
  auto rounded = records(d, tracker.diagnosticState());
  assert(rounded[0].find("S=3.576") != std::string::npos);
  assert(rounded[0].find("FS=1 VC=0 L=1") != std::string::npos);

  // Long timestamps / numeric fields must fit without truncating the trailing flags.
  d.checkMs = d.rawSampleMs = UINT32_MAX;
  d.speedMps = 999.999f;
  d.climbCms = INT32_MIN;
  d.rawG = 13.856f;
  auto s = tracker.diagnosticState();
  s.sampleMs = s.bucketStartMs = UINT32_MAX;
  s.filteredReady = true;
  s.filteredG = 8;
  auto longest = records(d, s);
  assert(longest[0].find("L=1") != std::string::npos);
  assert(longest[1].find("FV=1") != std::string::npos);
  assert(longest[2].find("E=1") != std::string::npos);
  for (const auto& line : longest) printf("LXLF%s\n", line.c_str());
  puts("g-load diagnostic checks passed");
}
