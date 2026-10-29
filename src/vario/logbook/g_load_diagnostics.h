#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "instruments/g_load_tracker.h"

// Captured at the actual once-per-second detector check. Threshold decisions are saved
// explicitly so rounding the human-readable speed cannot change a replay's result.
struct GLoadDiagnostics {
  static constexpr float MIN_SPEED_MPS = 8.0f * 0.44704f;
  static constexpr int32_t MIN_CLIMB_CMS = 50;
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
  bool ending = false;
  uint8_t launchPaths = 0;  // Shared FlightAutoStart confirmed path mask, not a raw activity flag.

  bool looksAirborne() const { return fast || vertical; }
  // Either positive indication vetoes landing; ground needs both measurements.
  bool detectorInputsValid() const { return looksAirborne() || (speedValid && climbValid); }
};

// Emit bounded V3 manufacturer records, each fitting within 76 bytes including LXLV.
// The sink adds the manufacturer prefix and CRLF. No allocation or persistent buffer here.
template <typename Sink>
void writeGLoadDiagnostics(const GLoadDiagnostics& d, const GLoadTracker::DiagnosticState& s,
                           const GLoadTracker::Interval& interval, Sink write) {
  char speed[8] = "NA";
  char raw[7] = "NA";
  char filtered[8] = "NA";
  if (d.speedValid && isfinite(d.speedMps) &&
      snprintf(speed, sizeof(speed), "%.3f", d.speedMps) >= (int)sizeof(speed))
    snprintf(speed, sizeof(speed), "OVF");
  if (d.rawValid && isfinite(d.rawG)) {
    if (snprintf(raw, sizeof(raw), "%.3f", d.rawG) >= (int)sizeof(raw)) {
      snprintf(raw, sizeof(raw), "OVF");
    }
  }
  const bool fresh = s.filteredReady && uint32_t(d.checkMs - s.sampleMs) <= GLoadTracker::FRESH_MS;
  if (fresh && isfinite(s.filteredG)) {
    snprintf(filtered, sizeof(filtered), "%.3f", s.filteredG);
  }
  char line[73];
  const uint32_t ignore = s.ignoreRemainingMs > GLoadTracker::TAKEOFF_IGNORE_MS
                              ? GLoadTracker::TAKEOFF_IGNORE_MS
                              : s.ignoreRemainingMs;
  snprintf(line, sizeof(line), "GL T=%lu S=%s C=%ld SV=%u CV=%u FS=%u VC=%u L=%u Z=%u",
           (unsigned long)d.checkMs, speed, (long)d.climbCms, d.speedValid, d.climbValid, d.fast,
           d.vertical, d.looksAirborne(), d.ending);
  write(line);
  snprintf(line, sizeof(line), "GA T=%lu RT=%lu FT=%lu R=%s F=%s RV=%u FV=%u",
           (unsigned long)d.checkMs, (unsigned long)d.rawSampleMs, (unsigned long)s.sampleMs, raw,
           filtered, d.rawValid, fresh);
  write(line);
  snprintf(line, sizeof(line), "GS T=%lu ON=%u A=%u G=%u I=%lu P=%u B=%lu E=%u H=%u D=%u",
           (unsigned long)d.checkMs, s.active, s.airborne, s.groundSeconds, (unsigned long)ignore,
           s.pending, (unsigned long)s.bucketStartMs, s.committed, 0u,
           static_cast<unsigned>(s.transition));
  write(line);
  snprintf(line, sizeof(line), "GQ T=%lu N=%u F=%u RJ=%u GP=%u CL=%u RS=%u",
           (unsigned long)d.checkMs, interval.samples, interval.filteredSamples, interval.rejected,
           interval.gaps, interval.clippingReports, interval.resets);
  write(line);
  snprintf(line, sizeof(line), "GX T=%lu RM=%u DT=%lu GT=%lu J=%u", (unsigned long)d.checkMs,
           interval.resetReasons, (unsigned long)interval.maxGapMs,
           (unsigned long)interval.maxGapEndMs, d.launchPaths);
  write(line);
  auto range = [&](const char* kind, const GLoadTracker::Range& r) {
    char lo[8] = "NA", hi[8] = "NA";
    if (r.valid) {
      snprintf(lo, sizeof(lo), "%.3f", r.min);
      snprintf(hi, sizeof(hi), "%.3f", r.max);
    }
    snprintf(line, sizeof(line), "%s T=%lu LO=%s HI=%s LT=%lu HT=%lu", kind,
             (unsigned long)d.checkMs, lo, hi, (unsigned long)r.minMs, (unsigned long)r.maxMs);
    write(line);
  };
  range("GR", interval.raw);
  range("GF", interval.filtered);
  range("GC", s.current);
  range("GP", s.previous);
  range("GE", s.extrema);
  range("GD", interval.discarded);
}
