#pragma once

#include <stdint.h>

// Shared five-second altitude history for the graph and trailing-minute net climb.
// Collection is independent of thermal detection, GPS and which display page is open.
class ThermalClimbHistory {
 public:
  static constexpr uint8_t CAPACITY = 40;
  static constexpr uint32_t SAMPLE_MS = 5000;
  // 1 - exp(-5 / 120): a two-minute EMA time constant on the fixed five-second grid.
  static constexpr float ORIGIN_AVERAGE_ALPHA = 0.040810543f;

  void reset() { *this = {}; }

  void update(uint32_t nowMs, int32_t altitudeCm) {
    if (!count_) {
      append(altitudeCm);
      newestMs_ = nowMs;
      bucketAltitudeCm_ = altitudeCm;
    } else {
      // Interpolate onto a fixed time grid rather than accumulating task scheduling jitter.
      const uint32_t elapsed = nowMs - lastUpdateMs_;
      while (nowMs - newestMs_ >= SAMPLE_MS) {
        newestMs_ += SAMPLE_MS;
        const uint32_t offset = newestMs_ - lastUpdateMs_;
        const int32_t sampleCm =
            elapsed ? latestAltitudeCm_ +
                          static_cast<int64_t>(altitudeCm - latestAltitudeCm_) * offset / elapsed
                    : altitudeCm;
        const float climbCms = (sampleCm - bucketAltitudeCm_) * (1000.0f / SAMPLE_MS);
        originAverageCms_ += (climbCms - originAverageCms_) * ORIGIN_AVERAGE_ALPHA;
        bucketAltitudeCm_ = sampleCm;
        append(sampleCm);
      }
    }
    latestAltitudeCm_ = altitudeCm;
    lastUpdateMs_ = nowMs;
  }

  // Origin motion uses its own slow EMA of net vertical rate, not an EMA of the one-minute
  // field. Start at zero after reset; update once per five-second bucket, regardless of page
  // refresh cadence. No extra altitude samples are retained.
  int32_t originAverageCms() const {
    return static_cast<int32_t>(originAverageCms_ + (originAverageCms_ >= 0 ? 0.5f : -0.5f));
  }

  uint8_t size() const { return count_; }
  int32_t currentAltitudeCm() const { return latestAltitudeCm_; }
  int32_t altitudeAgoCm(uint8_t intervals) const {
    if (!intervals) return latestAltitudeCm_;
    const int32_t older = altitudeM(intervals) * 100;
    const int32_t newer = altitudeM(intervals - 1) * 100;
    return older + static_cast<int64_t>(newer - older) * (lastUpdateMs_ - newestMs_) / SAMPLE_MS;
  }
  int16_t altitudeM(uint8_t age) const {
    return altitudesM_[(next_ + CAPACITY - 1 - age) % CAPACITY];
  }

  // The numerical field changes on the same five-second cadence as the graph. In the first
  // bucket, use the live altitude so the initial partial average need not wait five seconds.
  int32_t averageCms() const {
    if (!count_) return 0;
    if (count_ == 1) {
      const uint32_t elapsed = lastUpdateMs_ - newestMs_;
      return elapsed ? static_cast<int64_t>(latestAltitudeCm_ - bucketAltitudeCm_) * 1000 / elapsed
                     : 0;
    }
    const uint8_t intervals = count_ > 13 ? 12 : count_ - 1;
    return (static_cast<int32_t>(altitudeM(0)) - altitudeM(intervals)) * 100 / (intervals * 5);
  }

 private:
  void append(int32_t altitudeCm) {
    // Round once to meters; retain off-screen samples without pixel clamping or repeated shifts.
    const int32_t meters = (altitudeCm + (altitudeCm >= 0 ? 50 : -50)) / 100;
    altitudesM_[next_] = static_cast<int16_t>(meters < INT16_MIN   ? INT16_MIN
                                              : meters > INT16_MAX ? INT16_MAX
                                                                   : meters);
    next_ = (next_ + 1) % CAPACITY;
    if (count_ < CAPACITY) ++count_;
  }

  int16_t altitudesM_[CAPACITY] = {};
  uint32_t newestMs_ = 0;
  uint32_t lastUpdateMs_ = 0;
  int32_t latestAltitudeCm_ = 0;
  // The current bucket's initial altitude also serves the first partial numerical average.
  int32_t bucketAltitudeCm_ = 0;
  float originAverageCms_ = 0;
  uint8_t next_ = 0;
  uint8_t count_ = 0;
};

static_assert(sizeof(ThermalClimbHistory) <= 104,
              "Keep widget history and origin EMA within 104 bytes");
