#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// A bounded, exponentially fading history of ground speed and positive climb.
// Inputs are sampled by log_update(), normally once per second. Invalid sensors
// contribute nothing; either sensor can independently establish a launch.
class FlightAutoStart {
 public:
  // Sustained speed/climb scales correspond to a motion input of one. Movement
  // below that combined input cannot cross the start threshold, even indefinitely.
  static constexpr float SPEED_SCALE_MPH = 4.2f;
  static constexpr float CLIMB_SCALE_MPS = 0.7f;
  static constexpr float MEMORY_SECONDS = 2.4f;
  static constexpr uint32_t MIN_DETECTION_MS = 3000;
  static constexpr uint32_t MAX_UPDATE_GAP_MS = 2000;

  struct Sample {
    float speedMph = 0;
    float climbMps = 0;
    bool speedValid = false;
    bool climbValid = false;
  };

  float score() const { return score_; }

  void reset() {
    score_ = 0;
    initialized_ = false;
    observedMs_ = 0;
  }

  bool update(uint32_t nowMs, const Sample& sample) {
    const bool speedValid = sample.speedValid && std::isfinite(sample.speedMph);
    const bool climbValid = sample.climbValid && std::isfinite(sample.climbMps);
    if (!speedValid && !climbValid) {
      reset();
      return false;
    }

    if (!initialized_) {
      initialized_ = true;
      lastUpdateMs_ = nowMs;
      return false;  // The first reading establishes time, not an unseen interval.
    }
    const uint32_t elapsedMs = nowMs - lastUpdateMs_;
    lastUpdateMs_ = nowMs;
    if (elapsedMs > MAX_UPDATE_GAP_MS) {
      // A paused/sleeping logger cannot count the gap as sustained movement.
      reset();
      initialized_ = true;
      lastUpdateMs_ = nowMs;
      return false;
    }
    observedMs_ = std::min(MIN_DETECTION_MS, observedMs_ + elapsedMs);

    const float speed = speedValid ? std::max(0.0f, sample.speedMph) : 0;
    const float climb = climbValid ? std::max(0.0f, sample.climbMps) : 0;
    const float motion = speed / SPEED_SCALE_MPH + climb / CLIMB_SCALE_MPS;
    // At this maximum, an empty score reaches one after exactly three seconds.
    static const float maximumInput =
        1.0f /
        (1.0f - std::exp(-static_cast<float>(MIN_DETECTION_MS) / (1000.0f * MEMORY_SECONDS)));
    const float target = std::min(motion, maximumInput);
    const float retention = std::exp(-static_cast<float>(elapsedMs) / (1000.0f * MEMORY_SECONDS));
    score_ = target + (score_ - target) * retention;

    // Explicitly reject sub-threshold input: float rounding must not eventually
    // turn a steady input of exactly one into a launch. The small tolerance only
    // covers rounding at the three-second cap; the elapsed-time guard stays exact.
    return motion > 1.0f && observedMs_ >= MIN_DETECTION_MS && score_ >= 1.0f - 1e-6f;
  }

 private:
  float score_ = 0;
  uint32_t lastUpdateMs_ = 0;
  uint32_t observedMs_ = 0;
  bool initialized_ = false;
};
