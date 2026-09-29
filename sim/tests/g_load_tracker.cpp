#include "instruments/g_load_tracker.h"

#include <math.h>
#include <stdio.h>

#include <functional>

namespace {
  int failures = 0;

  void check(bool ok, const char* what, float value) {
    printf("%s  %-48s %.3f\n", ok ? "ok  " : "FAIL", what, value);
    if (!ok) failures++;
  }

  constexpr uint32_t SAMPLE_MS = 50;  // 20Hz, like the ICM-20948 DMP output

  // Plays `profile(msIntoSegment)` for `durationMs`, calling updateAirborne once per second.
  struct Flight {
    GLoadTracker tracker;
    uint32_t t = 1000;
    bool airborne = true;

    void play(uint32_t durationMs, const std::function<float(uint32_t)>& profile) {
      for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += SAMPLE_MS) {
        if (t % 1000 == 0) tracker.updateAirborne(airborne);
        tracker.addSample(profile(elapsed), t);
        t += SAMPLE_MS;
      }
    }
    void level(uint32_t durationMs, float g = 1.0f) {
      play(durationMs, [g](uint32_t) { return g; });
    }
  };

  float ramp(uint32_t elapsed, uint32_t rampMs, float from, float to) {
    if (elapsed >= rampMs) return to;
    return from + (to - from) * elapsed / rampMs;
  }
}  // namespace

int main() {
  {  // A steady 2.5g spiral (entered over 2s) is reported at its full load.
    Flight f;
    f.tracker.startFlight();
    f.level(30000);
    f.play(2000, [](uint32_t e) { return ramp(e, 2000, 1.0f, 2.5f); });
    f.level(10000, 2.5f);
    f.play(2000, [](uint32_t e) { return ramp(e, 2000, 2.5f, 1.0f); });
    f.level(90000);
    check(f.tracker.valid(), "spiral: extremes committed", f.tracker.maxG());
    check(fabsf(f.tracker.maxG() - 2.5f) < 0.05f, "spiral: max ~2.5g", f.tracker.maxG());
    check(fabsf(f.tracker.minG() - 1.0f) < 0.05f, "spiral: min ~1g", f.tracker.minG());
  }

  {  // A 0.3g drop lasting 1.5s (e.g. a frontal) shows up in min.
    Flight f;
    f.tracker.startFlight();
    f.level(30000);
    f.level(1500, 0.3f);
    f.level(90000);
    check(fabsf(f.tracker.minG() - 0.3f) < 0.05f, "drop: min ~0.3g", f.tracker.minG());
  }

  {  // Knocks against the device (1-2 samples at up to 4g) barely register.
    Flight f;
    f.tracker.startFlight();
    f.level(30000);
    f.level(SAMPLE_MS, 4.0f);
    f.level(5000);
    f.level(2 * SAMPLE_MS, 4.0f);
    f.level(5000);
    f.level(SAMPLE_MS, 0.0f);
    f.level(90000);
    check(f.tracker.maxG() < 1.1f, "knocks: max stays near 1g", f.tracker.maxG());
    check(f.tracker.minG() > 0.95f, "knocks: min stays near 1g", f.tracker.minG());
  }

  {  // Vibration (1g +/- 0.3 every sample) averages out.
    Flight f;
    f.tracker.startFlight();
    f.play(120000, [](uint32_t e) { return (e / SAMPLE_MS) % 2 ? 1.3f : 0.7f; });
    check(f.tracker.maxG() < 1.1f, "vibration: max near 1g", f.tracker.maxG());
    check(f.tracker.minG() > 0.9f, "vibration: min near 1g", f.tracker.minG());
  }

  {  // A launch surge in the first seconds of being airborne is ignored.
    Flight f;
    f.tracker.startFlight();
    f.level(5000);
    f.level(3000, 1.8f);
    f.level(120000);
    check(f.tracker.maxG() < 1.05f, "takeoff: surge ignored", f.tracker.maxG());
  }

  {  // A hard landing followed by the auto-stop wait never reaches the logbook.
    Flight f;
    f.tracker.startFlight();
    f.level(120000);
    f.level(1000, 3.0f);  // flare + touchdown
    f.airborne = false;
    f.level(21000);
    f.tracker.stopFlight();
    check(f.tracker.maxG() < 1.05f, "landing: impact discarded", f.tracker.maxG());
  }

  {  // Landing is detected even without stopping the log (e.g. manual stop much later).
    Flight f;
    f.tracker.startFlight();
    f.level(120000);
    f.level(1000, 3.0f);
    f.airborne = false;
    f.level(300000);
    check(f.tracker.maxG() < 1.05f, "ground: impact discarded after landing", f.tracker.maxG());
  }

  {  // Nothing counts before startFlight or while on the ground.
    Flight f;
    f.level(60000, 2.0f);
    check(!f.tracker.valid(), "inactive: nothing recorded", f.tracker.maxG());
    f.tracker.startFlight();
    f.airborne = false;
    f.level(120000, 2.0f);
    check(!f.tracker.valid(), "on ground: nothing recorded", f.tracker.maxG());
  }

  {  // Implausible samples are rejected outright.
    Flight f;
    f.tracker.startFlight();
    f.level(30000);
    f.level(1000, NAN);
    f.level(1000, 20.0f);
    f.level(90000);
    check(f.tracker.maxG() < 1.05f, "implausible: rejected", f.tracker.maxG());
  }

  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
