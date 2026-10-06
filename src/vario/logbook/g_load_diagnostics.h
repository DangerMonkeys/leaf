#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "instruments/g_load_tracker.h"

// Captured at the actual once-per-second detector check. Threshold decisions are saved
// explicitly so rounding the human-readable speed cannot change a replay's result.
struct GLoadDiagnostics {
  uint32_t checkMs = 0;
  float speedMps = 0;
  int32_t climbCms = 0;
  bool speedValid = false;
  bool climbValid = false;
  bool fast = false;
  bool vertical = false;
  float rawG = 0;
  bool rawValid = false;
  uint32_t rawSampleMs = 0;

  bool looksAirborne() const { return fast || vertical; }
};

// Emit three manufacturer L-record payloads, each fitting within 76 bytes including LXLF.
// The sink adds the manufacturer prefix and CRLF. No allocation or persistent buffer here.
template <typename Sink>
void writeGLoadDiagnostics(const GLoadDiagnostics& d, const GLoadTracker::DiagnosticState& s,
                           Sink write) {
  char speed[16] = "NA";
  char raw[7] = "NA";
  char filtered[6] = "NA";
  if (d.speedValid && isfinite(d.speedMps)) snprintf(speed, sizeof(speed), "%.3f", d.speedMps);
  if (d.rawValid && isfinite(d.rawG)) {
    if (snprintf(raw, sizeof(raw), "%.3f", d.rawG) >= (int)sizeof(raw)) {
      snprintf(raw, sizeof(raw), "OVF");
    }
  }
  if (s.filteredReady && isfinite(s.filteredG)) {
    snprintf(filtered, sizeof(filtered), "%.3f", s.filteredG);
  }
  char line[73];
  snprintf(line, sizeof(line), "GL T=%lu S=%s C=%ld SV=%u CV=%u FS=%u VC=%u L=%u",
           (unsigned long)d.checkMs, speed, (long)d.climbCms, d.speedValid, d.climbValid, d.fast,
           d.vertical, d.looksAirborne());
  write(line);
  snprintf(line, sizeof(line), "GA T=%lu RT=%lu FT=%lu R=%s F=%s RV=%u FV=%u",
           (unsigned long)d.checkMs, (unsigned long)d.rawSampleMs, (unsigned long)s.sampleMs, raw,
           filtered, d.rawValid, s.filteredReady);
  write(line);
  snprintf(line, sizeof(line), "GS T=%lu ON=%u A=%u G=%u I=%lu P=%u B=%lu E=%u",
           (unsigned long)d.checkMs, s.active, s.airborne, s.groundSeconds,
           (unsigned long)s.ignoreRemainingMs, s.pending, (unsigned long)s.bucketStartMs,
           s.committed);
  write(line);
}
