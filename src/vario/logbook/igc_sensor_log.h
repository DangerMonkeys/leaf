#pragma once

#include <math.h>
#include <stdio.h>

#include "instruments/g_load_tracker.h"

// Compact version-1 manufacturer records; see docs/dev-references/igc-sensor-records.md.
// Keep only the bounds already emitted for the two pending buckets, never sample history.
class IgcSensorLog {
 public:
  void reset() { *this = {}; }

  template <typename Emit>
  void finish(GLoadTracker::LogState state, Emit emit) {
    // Closing a track never accepts an unresolved tail, even if called directly.
    state.current = {};
    state.previous = {};
    update(state, 0, false, 0, false, emit);
  }

  template <typename Emit>
  void update(const GLoadTracker::LogState& state, uint32_t nowMs, bool temperatureValid,
              float temperatureC, bool writeSample, Emit emit) {
    for (auto& slot : slots_) {
      if (!slot.id || slot.id == state.current.id || slot.id == state.previous.id) continue;
      char line[16];
      snprintf(line, sizeof(line), "%c%06lX", slot.id <= state.committedThrough ? 'C' : 'D',
               (unsigned long)slot.id);
      emit(line);
      slot = {};
    }
    if (!writeSample) {
      // Once the track has started, an outage must not lose a bucket's extrema before
      // its commit. Peak-only records need no position or extra sample-history buffer.
      flush(state.previous, emit);
      flush(state.current, emit);
      return;
    }

    // The previous bucket can have acquired peaks after the last fix before rollover.
    // Flush those once before it becomes immutable, even if the new bucket has peaks too.
    flush(state.previous, emit);

    uint32_t sampleMs = state.fresh ? state.sampleMs : nowMs;
    uint32_t bucket = state.fresh ? state.sampleBucket : 0;
    uint16_t g = state.fresh ? milliG(state.filteredG) : 0xFFFF;
    bool extraLow = false;
    if (state.current.id && state.current.range.valid) {
      Slot& slot = find(state.current.id);
      const auto& range = state.current.range;
      const bool low = !slot.valid || milliG(range.min) < slot.min;
      const bool high = !slot.valid || milliG(range.max) > slot.max;
      if (low || high) {
        sampleMs = high ? range.maxMs : range.minMs;
        bucket = slot.id;
        g = milliG(high ? range.max : range.min);
        extraLow = low && high && milliG(range.min) != milliG(range.max);
      }
      remember(slot, range);
    }
    const int32_t deciC = temperatureValid && isfinite(temperatureC) && temperatureC >= -204.7f &&
                                  temperatureC <= 204.7f
                              ? lroundf(temperatureC * 10)
                              : -2048;
    temperatureValid = temperatureValid && deciC >= -2047 && deciC <= 2047;
    const uint16_t temperature = temperatureValid ? (deciC & 0xFFF) : 0x800;
    sample(sampleMs, bucket, g, temperature, emit);
    if (extraLow) {
      sample(state.current.range.minMs, state.current.id, milliG(state.current.range.min),
             temperature, emit);
    }
  }

 private:
  struct Slot {
    uint32_t id = 0;
    bool valid = false;
    uint16_t min = 0, max = 0;
  } slots_[2];

  static uint16_t milliG(float g) { return static_cast<uint16_t>(lroundf(g * 1000)); }
  Slot& find(uint32_t id) {
    for (auto& slot : slots_)
      if (slot.id == id) return slot;
    for (auto& slot : slots_)
      if (!slot.id) {
        slot.id = id;
        return slot;
      }
    // update() resolves departed buckets before installing current/previous identities.
    return slots_[0];
  }
  static void remember(Slot& slot, const GLoadTracker::Range& range) {
    slot.valid = true;
    slot.min = milliG(range.min);
    slot.max = milliG(range.max);
  }
  template <typename Emit>
  void flush(const GLoadTracker::LogBucket& bucket, Emit emit) {
    if (!bucket.id || !bucket.range.valid) return;
    Slot& slot = find(bucket.id);
    const auto& range = bucket.range;
    const bool low = !slot.valid || milliG(range.min) < slot.min;
    const bool high = !slot.valid || milliG(range.max) > slot.max;
    if (low) sample(range.minMs, slot.id, milliG(range.min), 0x800, emit);
    if (high && (!low || milliG(range.max) != milliG(range.min)))
      sample(range.maxMs, slot.id, milliG(range.max), 0x800, emit);
    remember(slot, range);
  }
  template <typename Emit>
  static void sample(uint32_t sampleMs, uint32_t bucket, uint16_t g, uint16_t temperature,
                     Emit emit) {
    char line[32];
    snprintf(line, sizeof(line), "S%08lX%06lX%04X%03X", (unsigned long)sampleMs,
             (unsigned long)bucket, g, temperature);
    emit(line);
  }
};
