#include "logbook/g_load_diagnostics.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>

static std::vector<std::string> records(const GLoadDiagnostics& d,
                                        const GLoadTracker::DiagnosticState& s,
                                        const GLoadTracker::Interval& q = {}) {
  std::vector<std::string> lines;
  writeGLoadDiagnostics(d, s, q, [&](const char* line) { lines.emplace_back(line); });
  assert(lines.size() == 11);
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
  assert(!d.detectorInputsValid());
  d.climbValid = true;
  assert(!d.detectorInputsValid());  // Missing speed cannot prove ground.
  d.vertical = true;
  assert(d.detectorInputsValid());  // A positive climb indication still proves activity.
  d.climbValid = d.vertical = false;
  auto cold = records(d, tracker.diagnosticState());
  assert(cold[0] == "GL T=1000 S=NA C=0 SV=0 CV=0 FS=0 VC=0 L=0 Z=0");
  assert(cold[1] == "GA T=1000 RT=950 FT=0 R=1.820 F=NA RV=1 FV=0");
  assert(cold[5] == "GR T=1000 LO=NA HI=NA LT=0 HT=0");

  tracker.startFlight();
  for (int i = 0; i < 3; ++i) tracker.updateFlightState(true, true);
  assert(tracker.diagnosticState().ignoreRemainingMs == 15000);
  uint32_t t = 1000;
  for (; t < 90000; t += 50) tracker.addSample(1, t);
  assert(tracker.valid());
  tracker.takeInterval();
  for (int i = 0; i < 19; ++i) tracker.updateFlightState(false, false);
  assert(tracker.diagnosticState().airborne);
  tracker.updateFlightState(false, false);
  auto landed = tracker.diagnosticState();
  assert(!landed.airborne && !landed.pending && landed.committed);
  auto discarded = tracker.takeInterval();
  assert(discarded.discarded.valid);
  d.checkMs = t;
  auto landing = records(d, landed, discarded);
  assert(landing[2].find("D=2") != std::string::npos);
  assert(landing[10].find("LO=1.000 HI=1.000") != std::string::npos);
  tracker.addSample(1, t + 1000);
  assert(!tracker.filteredValid(t + 1000));

  d.speedValid = true;
  d.speedMps = 3.5764f;
  d.fast = true;
  d.climbValid = true;
  d.climbCms = -50;
  auto rounded = records(d, tracker.diagnosticState());
  assert(rounded[0].find("S=3.576") != std::string::npos);
  assert(rounded[0].find("FS=1 VC=0 L=1") != std::string::npos);

  d.checkMs = d.rawSampleMs = UINT32_MAX;
  d.speedMps = 999.999f;
  d.climbCms = INT32_MIN;
  d.rawG = 13.856f;
  d.ending = true;
  auto s = tracker.diagnosticState();
  s.sampleMs = s.bucketStartMs = UINT32_MAX;
  s.filteredReady = true;
  s.filteredG = 13.856f;
  auto q = discarded;
  q.samples = q.filteredSamples = q.rejected = q.gaps = q.clippingReports = q.resets = UINT16_MAX;
  q.raw.add(13.856f, UINT32_MAX);
  q.filtered = q.raw;
  q.resetReasons = UINT16_MAX;
  q.maxGapMs = q.maxGapEndMs = UINT32_MAX;
  d.launchPaths = 7;
  auto longest = records(d, s, q);
  assert(longest[0].find("Z=1") != std::string::npos);
  assert(longest[1].find("F=13.856 RV=1 FV=1") != std::string::npos);
  assert(longest[2].find("D=2") != std::string::npos);
  assert(longest[3].find("RS=65535") != std::string::npos);
  assert(longest[4] == "GX T=4294967295 RM=65535 DT=4294967295 GT=4294967295 J=7");
  assert(longest[5].find("HT=4294967295") != std::string::npos);
  d.checkMs = GLoadTracker::FRESH_MS + 1;
  s.sampleMs = 0;
  auto stale = records(d, s);
  assert(stale[1].find("F=NA RV=1 FV=0") != std::string::npos);
  puts("V3 g-load diagnostic length, freshness, interval and discard checks passed");
}
