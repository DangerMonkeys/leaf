#pragma once

#include <stdint.h>

#include "instruments/vario_timing.h"

// Sustained load, independent of the raw IMU value used by auto-stop and self-test.
// Flight buckets and small quality counters are bounded; no diagnostic sample-history ranges.
class GLoadTracker {
 public:
  static constexpr float MAX_SLEW_G_PER_S = 3.0f;
  static constexpr float MAX_PLAUSIBLE_G = 14.0f;  // Resultant of three +/-8g axes.
  static constexpr uint8_t AVERAGE_SAMPLES = 10;   // Approximately 0.5s at 20Hz.
  static constexpr uint32_t MAX_SAMPLE_GAP_MS = 500;
  static constexpr uint32_t FRESH_MS = vario_timing::SENSOR_FRESH_MS;
  static constexpr uint32_t TAKEOFF_IGNORE_MS = 15000;
  static constexpr uint32_t PENDING_BUCKET_MS = 30000;  // Hold back 30-60s.
  static constexpr uint8_t GROUND_SECONDS_TO_LAND = 20;
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
    uint16_t samples = 0, filteredSamples = 0, rejected = 0;
    uint16_t gaps = 0, clippingReports = 0, resets = 0;
    uint16_t resetReasons = 0;
    uint32_t maxGapMs = 0, maxGapEndMs = 0;
  };
  // Stable identities let a track reader apply the same delayed acceptance as the logbook.
  // Reuse the two existing pending ranges; this is not a sample-history cache.
  struct LogBucket {
    uint32_t id = 0;
    Range range;
  };
  struct LogState {
    LogBucket current, previous;
    uint32_t committedThrough = 0, sampleBucket = 0, sampleMs = 0;
    float filteredG = 1;
    bool fresh = false;
  };
  LogState logState(uint32_t nowMs) const {
    return {{currentId_, current_}, {previousId_, previous_},
            committedThrough_,      sampleBucket_,
            lastSampleMs_,          filtered_,
            filteredValid(nowMs)};
  }
  // Interval bitmask. SampleGap is an automatic averaging restart, not an explicit reset.
  enum class ResetReason : uint16_t {
    Unspecified = 1,
    Sleep = 2,
    Wake = 4,
    SensorMessage = 8,
    SourceChange = 16,
    MissingAcceleration = 32,
    InvalidGravity = 64,
    BiasCalibration = 128,
    SampleGap = 256
  };
  enum class Transition : uint8_t { None, Airborne, Landed, Unknown, Started, Stopped };
  struct DiagnosticState {
    bool active, airborne;
    uint8_t groundSeconds;
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
    return {active_,          airborne_,      groundSeconds_,
            remaining,        bucketStarted_, bucketStarted_ ? bucketStartMs_ : 0,
            committed_.valid, filteredReady_, lastSampleMs_,
            filtered_,        transition_,    current_,
            previous_,        committed_};
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
    nextBucketId_ = 0;
    committedThrough_ = 0;
    active_ = true;
    airborne_ = takeoffGuardArmed_ = false;
    groundSeconds_ = 0;
    transition_ = Transition::Started;
  }
  void stopFlight() {
    discardPending();
    active_ = airborne_ = takeoffGuardArmed_ = false;
    groundSeconds_ = 0;
    transition_ = Transition::Stopped;
  }
  // Launch is already confirmed by the shared motion-window detector. Activity is
  // only a landing veto: failing a launch test during slow flight must not land.
  void updateFlightState(bool launchConfirmed, bool activeMotion, bool inputsValid = true) {
    transition_ = Transition::None;
    if (!active_) return;
    if (!airborne_) {
      if (launchConfirmed) {
        airborne_ = takeoffGuardArmed_ = true;
        groundSeconds_ = 0;
        transition_ = Transition::Airborne;
      } else if (!inputsValid) {
        transition_ = Transition::Unknown;
      }
      return;
    }
    if (!inputsValid) {
      groundSeconds_ = 0;
      transition_ = Transition::Unknown;
      return;
    }
    if (activeMotion) {
      groundSeconds_ = 0;
    } else {
      if (airborne_ && ++groundSeconds_ >= GROUND_SECONDS_TO_LAND) {
        airborne_ = takeoffGuardArmed_ = false;
        discardPending();
        transition_ = Transition::Landed;
      }
    }
  }
  // Discontinuities invalidate live filtering without erasing committed flight history.
  void resetFilter(ResetReason reason = ResetReason::Unspecified) {
    increment(interval_.resets);
    interval_.resetReasons |= static_cast<uint16_t>(reason);
    hasSample_ = filteredReady_ = false;
    sampleBucket_ = 0;
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
    if (haveSampleTime_ && dt > interval_.maxGapMs) {
      interval_.maxGapMs = dt;
      interval_.maxGapEndMs = tMs;
    }
    if (haveSampleTime_ && dt > MAX_SAMPLE_GAP_MS) {
      increment(interval_.gaps);
      interval_.resetReasons |= static_cast<uint16_t>(ResetReason::SampleGap);
    }
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
    sampleBucket_ = 0;
    increment(interval_.filteredSamples);
    if (!active_ || !airborne_) return;
    if (takeoffGuardArmed_) {
      takeoffGuardArmed_ = false;
      countFromMs_ = tMs + TAKEOFF_IGNORE_MS;
    }
    if (int32_t(tMs - countFromMs_) < 0) return;
    rollBuckets(tMs);
    current_.add(filtered_, tMs);
    sampleBucket_ = currentId_;
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
      currentId_ = ++nextBucketId_;
    } else if (tMs - bucketStartMs_ >= PENDING_BUCKET_MS) {
      committed_.add(previous_);
      if (previousId_) committedThrough_ = previousId_;
      previous_ = current_;
      previousId_ = currentId_;
      current_ = Range();
      currentId_ = ++nextBucketId_;
      bucketStartMs_ = tMs;
    }
  }
  void discardPending() {
    previous_ = current_ = Range();
    bucketStarted_ = false;
    currentId_ = previousId_ = sampleBucket_ = 0;
  }
  bool active_ = false, airborne_ = false, takeoffGuardArmed_ = false;
  uint8_t groundSeconds_ = 0;
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
  uint32_t nextBucketId_ = 0, currentId_ = 0, previousId_ = 0;
  uint32_t committedThrough_ = 0, sampleBucket_ = 0;
  Interval interval_;
  Transition transition_ = Transition::None;
};
