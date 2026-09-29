#pragma once

#include <stdint.h>

// Tracks the peak sustained g-load of a flight for the logbook (min/max accel).
//
// Every IMU sample (~20Hz) passes through:
//   1. a slew limiter, so one- or two-sample knocks barely move the value while the gradual onset
//      of a spiral, wingover or surge passes through almost untouched;
//   2. a 0.5s moving average, so the value reflects sustained load rather than vibration.
//
// Only samples taken while airborne count, and not during the first seconds after becoming
// airborne (launch run and first surge).  Extremes are held back for a while before being
// committed, and the held-back part is discarded on landing or when the flight stops, so the
// touchdown and packing up never reach the logbook.
//
// The raw accelerometer value (IMU::getAccel) is unaffected by this filtering.
class GLoadTracker {
 public:
  static constexpr float MAX_SLEW_G_PER_S = 3.0f;
  static constexpr float MAX_PLAUSIBLE_G = 8.0f;
  static constexpr uint8_t AVERAGE_SAMPLES = 10;  // 0.5s at 20Hz
  static constexpr uint32_t MAX_SAMPLE_GAP_MS = 500;
  static constexpr uint32_t TAKEOFF_IGNORE_MS = 15000;
  static constexpr uint32_t PENDING_BUCKET_MS = 30000;  // Two buckets held back: 30-60s
  static constexpr uint8_t GROUND_SECONDS_TO_LAND = 5;

  // Start a new flight: clears all extremes.  Nothing counts until airborne.
  void startFlight() {
    discardPending();
    committed_ = Range();
    active_ = true;
    airborne_ = false;
    takeoffGuardArmed_ = false;
    groundSeconds_ = 0;
  }

  // End the flight, discarding extremes that have not been committed yet.
  void stopFlight() {
    discardPending();
    active_ = false;
    airborne_ = false;
  }

  // Called about once per second with whether the flight currently looks airborne.  Becoming
  // airborne restarts the takeoff guard; landing (debounced) discards the held-back extremes.
  void updateAirborne(bool airborneNow) {
    if (!active_) return;
    if (airborneNow) {
      groundSeconds_ = 0;
      if (!airborne_) {
        airborne_ = true;
        takeoffGuardArmed_ = true;
      }
    } else if (airborne_ && ++groundSeconds_ >= GROUND_SECONDS_TO_LAND) {
      airborne_ = false;
      discardPending();
    }
  }

  // Feed one accelerometer magnitude sample, in g.
  void addSample(float accelG, uint32_t tMs) {
    if (!(accelG >= 0.0f && accelG <= MAX_PLAUSIBLE_G)) return;  // also rejects NaN

    uint32_t dt = tMs - lastSampleMs_;
    if (!hasSample_ || dt == 0 || dt > MAX_SAMPLE_GAP_MS) {
      // (Re)start the filter at rest; a gap means the history no longer describes the present.
      hasSample_ = true;
      slewed_ = 1.0f;
      averageCount_ = 0;
      averageIndex_ = 0;
      dt = 0;
    }
    lastSampleMs_ = tMs;

    float maxStep = MAX_SLEW_G_PER_S * dt * 0.001f;
    float step = accelG - slewed_;
    if (step > maxStep) step = maxStep;
    if (step < -maxStep) step = -maxStep;
    slewed_ += step;

    window_[averageIndex_] = slewed_;
    averageIndex_ = (averageIndex_ + 1) % AVERAGE_SAMPLES;
    if (averageCount_ < AVERAGE_SAMPLES) {
      averageCount_++;
      return;
    }
    float sum = 0;
    for (float v : window_) sum += v;
    filtered_ = sum / AVERAGE_SAMPLES;

    if (!active_ || !airborne_) return;
    if (takeoffGuardArmed_) {
      // Timed on the sample clock, which may differ from millis() when samples are injected.
      takeoffGuardArmed_ = false;
      countFromMs_ = tMs + TAKEOFF_IGNORE_MS;
    }
    if ((int32_t)(tMs - countFromMs_) < 0) return;
    rollBuckets(tMs);
    current_.add(filtered_);
  }

  // Latest filtered (slew-limited, averaged) value in g, 1g until the window has filled.
  float filtered() const { return filtered_; }

  // Committed flight extremes; valid() is false until at least one bucket was committed.
  bool valid() const { return committed_.valid; }
  float maxG() const { return committed_.max; }
  float minG() const { return committed_.min; }

 private:
  struct Range {
    bool valid = false;
    float min = 1.0f;
    float max = 1.0f;

    void add(float v) {
      if (!valid) {
        valid = true;
        min = max = v;
        return;
      }
      if (v > max) max = v;
      if (v < min) min = v;
    }
    void add(const Range& other) {
      if (!other.valid) return;
      add(other.min);
      add(other.max);
    }
  };

  void rollBuckets(uint32_t tMs) {
    if (!bucketStarted_) {
      bucketStarted_ = true;
      bucketStartMs_ = tMs;
    } else if (tMs - bucketStartMs_ >= PENDING_BUCKET_MS) {
      committed_.add(previous_);
      previous_ = current_;
      current_ = Range();
      bucketStartMs_ = tMs;
    }
  }

  void discardPending() {
    previous_ = Range();
    current_ = Range();
    bucketStarted_ = false;
  }

  bool active_ = false;
  bool airborne_ = false;
  uint8_t groundSeconds_ = 0;
  bool takeoffGuardArmed_ = false;
  uint32_t countFromMs_ = 0;

  bool hasSample_ = false;
  uint32_t lastSampleMs_ = 0;
  float slewed_ = 1.0f;
  float window_[AVERAGE_SAMPLES] = {};
  uint8_t averageIndex_ = 0;
  uint8_t averageCount_ = 0;
  float filtered_ = 1.0f;

  bool bucketStarted_ = false;
  uint32_t bucketStartMs_ = 0;
  Range current_;
  Range previous_;
  Range committed_;
};
