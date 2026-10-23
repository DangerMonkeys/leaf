#pragma once

#include <stdint.h>

// Sustained load, independent of the raw IMU value used by auto-stop and self-test.
// All storage is bounded; diagnostics summarize samples rather than writing at IMU rate.
class GLoadTracker {
 public:
  static constexpr float MAX_SLEW_G_PER_S = 3.0f;
  static constexpr float MAX_PLAUSIBLE_G = 14.0f;  // Resultant of three +/-8g axes.
  static constexpr uint8_t AVERAGE_SAMPLES = 10;   // Approximately 0.5s at 20Hz.
  static constexpr uint32_t MAX_SAMPLE_GAP_MS = 500;
  static constexpr uint32_t FRESH_MS = 250;
  static constexpr uint32_t TAKEOFF_IGNORE_MS = 15000;
  static constexpr uint32_t PENDING_BUCKET_MS = 30000;  // Hold back 30-60s.
  static constexpr uint8_t GROUND_SECONDS_TO_LAND = 20;
  static constexpr uint8_t AIR_SECONDS_TO_FLY = 3;
  static_assert(GROUND_SECONDS_TO_LAND * 1000 < PENDING_BUCKET_MS,
                "landing must be confirmed before the touchdown can be committed");

  struct Range {
    bool valid = false;
    float min = 1.0f, max = 1.0f;
    uint32_t minMs = 0, maxMs = 0;
    void add(float v, uint32_t t) {
      if (!valid) {
        valid = true;
        min = max = v;
        minMs = maxMs = t;
      } else {
        if (v < min) {
          min = v;
          minMs = t;
        }
        if (v > max) {
          max = v;
          maxMs = t;
        }
      }
    }
    void add(const Range& other) {
      if (!other.valid) return;
      add(other.min, other.minMs);
      add(other.max, other.maxMs);
    }
  };
  struct Interval {
    Range raw, filtered, discarded;
    uint16_t samples = 0, filteredSamples = 0, rejected = 0;
    uint16_t gaps = 0, clippingReports = 0, resets = 0;
  };
  enum class Transition : uint8_t { None, Airborne, Landed, Unknown, Started, Stopped };
  struct DiagnosticState {
    bool active, airborne;
    uint8_t groundSeconds, airSeconds;
    uint32_t ignoreRemainingMs;
    bool pending;
    uint32_t bucketStartMs;
    bool committed, filteredReady;
    uint32_t sampleMs;
    float filteredG;
    Transition transition;
    Range current, previous, extrema;
  };
  DiagnosticState diagnosticState() const {
    uint32_t remaining = 0;
    if (active_ && airborne_) {
      if (takeoffGuardArmed_)
        remaining = TAKEOFF_IGNORE_MS;
      else if (int32_t(countFromMs_ - lastSampleMs_) > 0)
        remaining = countFromMs_ - lastSampleMs_;
    }
    return {active_,
            airborne_,
            groundSeconds_,
            airSeconds_,
            remaining,
            bucketStarted_,
            bucketStarted_ ? bucketStartMs_ : 0,
            committed_.valid,
            filteredReady_,
            lastSampleMs_,
            filtered_,
            transition_,
            current_,
            previous_,
            committed_};
  }
  Interval takeInterval() {
    const Interval result = interval_;
    interval_ = Interval();
    return result;
  }
  void startFlight() {
    discardPending();
    interval_ = Interval();
    committed_ = Range();
    active_ = true;
    airborne_ = takeoffGuardArmed_ = false;
    groundSeconds_ = airSeconds_ = 0;
    transition_ = Transition::Started;
  }
  void stopFlight() {
    discardPending();
    active_ = airborne_ = takeoffGuardArmed_ = false;
    groundSeconds_ = airSeconds_ = 0;
    transition_ = Transition::Stopped;
  }
  // Check counts, approximately once per second. Unknown input must not invent a landing.
  void updateAirborne(bool airborneNow, bool inputsValid = true) {
    transition_ = Transition::None;
    if (!active_) return;
    if (!inputsValid) {
      groundSeconds_ = airSeconds_ = 0;
      transition_ = Transition::Unknown;
      return;
    }
    if (airborneNow) {
      groundSeconds_ = 0;
      if (airSeconds_ < AIR_SECONDS_TO_FLY) ++airSeconds_;
      if (!airborne_ && airSeconds_ >= AIR_SECONDS_TO_FLY) {
        airborne_ = true;
        takeoffGuardArmed_ = true;
        transition_ = Transition::Airborne;
      }
    } else {
      airSeconds_ = 0;
      if (airborne_ && ++groundSeconds_ >= GROUND_SECONDS_TO_LAND) {
        airborne_ = takeoffGuardArmed_ = false;
        discardPending();
        transition_ = Transition::Landed;
      }
    }
  }
  // Discontinuities invalidate live filtering without erasing committed flight history.
  void resetFilter() {
    increment(interval_.resets);
    hasSample_ = filteredReady_ = false;
    averageCount_ = averageIndex_ = 0;
  }
  void recordClipping() { increment(interval_.clippingReports); }
  // maxMagnitudeG is derived from the active chip's per-axis range by IMU.
  void addSample(float accelG, uint32_t tMs, float maxMagnitudeG = MAX_PLAUSIBLE_G) {
    if (!(accelG >= 0.0f && accelG <= maxMagnitudeG && accelG <= MAX_PLAUSIBLE_G)) {
      increment(interval_.rejected);
      return;
    }
    uint32_t dt = tMs - lastSampleMs_;
    if (haveSampleTime_ && int32_t(dt) <= 0) {
      increment(interval_.rejected);
      return;
    }
    if (haveSampleTime_ && dt > MAX_SAMPLE_GAP_MS) increment(interval_.gaps);
    interval_.raw.add(accelG, tMs);
    increment(interval_.samples);
    if (!hasSample_ || dt > MAX_SAMPLE_GAP_MS) {
      hasSample_ = true;
      slewed_ = 1.0f;
      averageCount_ = averageIndex_ = 0;
      filteredReady_ = false;
      dt = 0;
    }
    haveSampleTime_ = true;
    lastSampleMs_ = tMs;
    const float maxStep = MAX_SLEW_G_PER_S * dt * 0.001f;
    float step = accelG - slewed_;
    if (step > maxStep) step = maxStep;
    if (step < -maxStep) step = -maxStep;
    slewed_ += step;
    window_[averageIndex_] = slewed_;
    averageIndex_ = (averageIndex_ + 1) % AVERAGE_SAMPLES;
    if (averageCount_ < AVERAGE_SAMPLES) ++averageCount_;
    if (averageCount_ < AVERAGE_SAMPLES) return;
    float sum = 0;
    for (float v : window_) sum += v;
    filtered_ = sum / AVERAGE_SAMPLES;
    filteredReady_ = true;
    interval_.filtered.add(filtered_, tMs);
    increment(interval_.filteredSamples);
    if (!active_ || !airborne_) return;
    if (takeoffGuardArmed_) {
      takeoffGuardArmed_ = false;
      countFromMs_ = tMs + TAKEOFF_IGNORE_MS;
    }
    if (int32_t(tMs - countFromMs_) < 0) return;
    rollBuckets(tMs);
    current_.add(filtered_, tMs);
  }
  bool filteredValid(uint32_t nowMs) const {
    return filteredReady_ && uint32_t(nowMs - lastSampleMs_) <= FRESH_MS;
  }
  float filtered() const { return filtered_; }
  bool valid() const { return committed_.valid; }
  float maxG() const { return committed_.max; }
  float minG() const { return committed_.min; }

 private:
  static void increment(uint16_t& count) {
    if (count != UINT16_MAX) ++count;
  }
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
    interval_.discarded.add(previous_);
    interval_.discarded.add(current_);
    previous_ = current_ = Range();
    bucketStarted_ = false;
  }
  bool active_ = false, airborne_ = false, takeoffGuardArmed_ = false;
  uint8_t groundSeconds_ = 0, airSeconds_ = 0;
  uint32_t countFromMs_ = 0;
  bool hasSample_ = false, haveSampleTime_ = false;
  uint32_t lastSampleMs_ = 0;
  float slewed_ = 1.0f;
  float window_[AVERAGE_SAMPLES] = {};
  uint8_t averageIndex_ = 0, averageCount_ = 0;
  bool filteredReady_ = false;
  float filtered_ = 1.0f;
  bool bucketStarted_ = false;
  uint32_t bucketStartMs_ = 0;
  Range current_, previous_, committed_;
  Interval interval_;
  Transition transition_ = Transition::None;
};
