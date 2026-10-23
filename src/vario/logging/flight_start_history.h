#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "logging/flight_auto_start.h"

enum class FlightStartCause : uint8_t { Manual, Automatic };

// Temporary 1 Hz launch diagnostics, frozen at timer start until the track opens.
class FlightStartHistory {
 public:
  static constexpr size_t CAPACITY = 30;
  enum Flag : uint8_t { SpeedValid = 1, ClimbValid = 2, Enabled = 4, Inhibited = 8 };
  struct Entry {
    uint32_t atMs = 0;
    float speedMph = 0;
    float climbMps = 0;
    float score = 0;
    uint8_t flags = 0;
  };

  void reset() {
    count_ = next_ = 0;
    frozen_ = false;
  }

  void record(uint32_t nowMs, const FlightAutoStart::Sample& sample, float score, bool enabled,
              bool inhibited) {
    if (frozen_) return;
    if (count_ && uint32_t(nowMs - newest().atMs) > FlightAutoStart::MAX_UPDATE_GAP_MS) reset();
    // Replace within a clock-second, retaining the exact triggering/manual-start
    // reading without adding a second row when buttons or tests call us again.
    const bool replace = count_ && nowMs / 1000 == newest().atMs / 1000;
    const size_t index = replace ? (next_ + CAPACITY - 1) % CAPACITY : next_;
    const bool speedValid = sample.speedValid && std::isfinite(sample.speedMph);
    const bool climbValid = sample.climbValid && std::isfinite(sample.climbMps);
    entries_[index] = {
        nowMs, speedValid ? sample.speedMph : 0, climbValid ? sample.climbMps : 0, score,
        static_cast<uint8_t>((speedValid ? SpeedValid : 0) | (climbValid ? ClimbValid : 0) |
                             (enabled ? Enabled : 0) | (inhibited ? Inhibited : 0))};
    if (!replace) {
      next_ = (next_ + 1) % CAPACITY;
      if (count_ < CAPACITY) ++count_;
    }
  }

  void freeze(uint32_t nowMs, FlightStartCause cause) {
    frozen_ = true;
    startedAtMs_ = nowMs;
    cause_ = cause;
  }
  bool frozen() const { return frozen_; }
  size_t size() const { return count_; }
  const Entry& at(size_t index) const {
    return entries_[(next_ + CAPACITY - count_ + index) % CAPACITY];
  }
  uint32_t startedAtMs() const { return startedAtMs_; }
  FlightStartCause cause() const { return cause_; }

 private:
  const Entry& newest() const { return entries_[(next_ + CAPACITY - 1) % CAPACITY]; }
  std::array<Entry, CAPACITY> entries_{};
  size_t next_ = 0;
  size_t count_ = 0;
  bool frozen_ = false;
  uint32_t startedAtMs_ = 0;
  FlightStartCause cause_ = FlightStartCause::Manual;
};
