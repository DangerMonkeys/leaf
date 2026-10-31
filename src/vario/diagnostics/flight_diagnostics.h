#pragma once

#include <stdint.h>
#include <stdio.h>
#include <mutex>

// Event counters only: no sensor history or I/O on the producer path.
// The foreground logger drains a bounded snapshot at most once every 30 seconds.
class FlightDiagnostics {
 public:
  static constexpr uint32_t REPORT_MS = 30000;
  static constexpr uint32_t SLOW_WRITE_MS = 100;
  static constexpr uint32_t GAP_MS = 500;
  enum class Kind : uint8_t {
    ImuReset,
    GravityReset,
    KalmanReset,
    FifoReset,
    MotionGap,
    PressureGap,
    SlowWrite,
    Count
  };
  struct Event {
    uint32_t atMs = 0, durationMs = 0;
    uint16_t count = 0, reasons = 0;
  };

  void begin(uint32_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    clear();
    active_ = true;
    lastReportMs_ = nowMs;
  }
  void end() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
    clear();
  }
  void record(Kind kind, uint32_t atMs, uint32_t durationMs = 0, uint16_t reasons = 0,
              uint16_t count = 1) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || kind >= Kind::Count || !count) return;
    auto& event = events_[static_cast<unsigned>(kind)];
    const uint32_t sum = uint32_t(event.count) + count;
    event.count = sum > UINT16_MAX ? UINT16_MAX : sum;
    event.reasons |= reasons;
    // Keep the worst duration's original timestamp, or the latest tied/reset event.
    if (durationMs >= event.durationMs) {
      event.atMs = atMs;
      event.durationMs = durationMs;
    }
  }
  template <typename Sink>
  void report(uint32_t nowMs, Sink write, bool final = false) {
    // Drain before writing: a slow diagnostic write becomes a later event, never recursion.
    Event snapshot[static_cast<unsigned>(Kind::Count)];
    {
      // UDP injection can publish sensor events on another task. Never hold this
      // lock across SD I/O or the producer would inherit the write stall.
      std::lock_guard<std::mutex> lock(mutex_);
      if (!active_ || (!final && uint32_t(nowMs - lastReportMs_) < REPORT_MS)) return;
      lastReportMs_ = nowMs;
      for (unsigned i = 0; i < static_cast<unsigned>(Kind::Count); ++i) snapshot[i] = events_[i];
      clear();
    }
    static const char* const names[] = {"IMU",    "GRAVITY",  "KALMAN", "FIFO",
                                        "MOTION", "PRESSURE", "WRITE"};
    char line[73];
    for (unsigned i = 0; i < static_cast<unsigned>(Kind::Count); ++i) {
      const auto& event = snapshot[i];
      if (!event.count) continue;
      snprintf(line, sizeof(line), "FD K=%s N=%u T=%lu DT=%lu R=%u", names[i], event.count,
               (unsigned long)event.atMs, (unsigned long)event.durationMs, event.reasons);
      write(line);
    }
  }

 private:
  void clear() {
    for (auto& event : events_) event = Event();
  }
  Event events_[static_cast<unsigned>(Kind::Count)]{};
  uint32_t lastReportMs_ = 0;
  bool active_ = false;
  std::mutex mutex_;
};

extern FlightDiagnostics flightDiagnostics;
