#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Three-second motion windows sampled while idle. Pressure altitude corroborates
// vertical velocity; GPS displacement corroborates speed.
class FlightAutoStart {
 public:
  static constexpr uint32_t WINDOW_MS = 3000;
  static constexpr uint32_t CONFIRM_MS = 1000;
  static constexpr uint32_t MAX_UPDATE_GAP_MS = 2000;
  static constexpr float FORWARD_SPEED_MPS = 2.2352f;  // Exactly 5 mph.
  static constexpr float FORWARD_DISTANCE_M = 5.0f;
  static constexpr float VERTICAL_DISTANCE_M = 3.0f;
  static constexpr float VERTICAL_CLIMB_MPS = 0.5f;  // Strictly greater than this.
  static constexpr float BLEND_MIN_FORWARD = 0.7f;
  static constexpr float BLEND_MIN_VERTICAL = 0.5f;
  static constexpr size_t CAPACITY = 32;

  enum Path : uint8_t { None = 0, Forward = 1, Vertical = 2, Blended = 4 };

  struct Sample {
    float speedMps = 0;
    float climbMps = 0;
    float altitudeM = 0;  // Unadjusted pressure altitude, never GPS/QNH altitude.
    double latitude = 0;
    double longitude = 0;
    bool speedValid = false;
    bool climbValid = false;
    bool altitudeValid = false;
    bool positionValid = false;
  };

  struct Window {
    float meanSpeedMps = 0;
    float meanClimbMps = 0;     // Average signed samples before taking magnitude.
    float altitudeChangeM = 0;  // Signed endpoint difference, not accumulated movement.
    float displacementM = 0;
    bool speedValid = false;
    bool climbValid = false;
    bool altitudeValid = false;
    bool positionValid = false;
  };

  static Sample validated(Sample s) {
    s.speedValid = s.speedValid && std::isfinite(s.speedMps) && s.speedMps >= 0;
    s.climbValid = s.climbValid && std::isfinite(s.climbMps);
    s.altitudeValid = s.altitudeValid && std::isfinite(s.altitudeM);
    s.positionValid = s.positionValid && std::isfinite(s.latitude) && std::isfinite(s.longitude) &&
                      std::abs(s.latitude) <= 90 && std::abs(s.longitude) <= 180;
    return s;
  }

  const Window& window() const { return window_; }
  float score() const { return score_; }  // Unconfirmed evidence, not a start decision.
  uint8_t candidates() const { return candidates_; }
  uint8_t triggered() const { return triggered_; }

  void reset() {
    count_ = 0;
    window_ = Window();
    score_ = 0;
    candidates_ = triggered_ = None;
    forwardConfirmation_ = Confirmation();
    verticalConfirmation_ = Confirmation();
    blendConfirmation_ = Confirmation();
  }

  bool update(uint32_t nowMs, const Sample& input) {
    const Sample sample = validated(input);
    if (count_ && uint32_t(nowMs - entries_[count_ - 1].atMs) > MAX_UPDATE_GAP_MS)
      reset();  // Backwards time also resets; unsigned differences allow millis rollover.

    if (count_ && entries_[count_ - 1].atMs == nowMs) {
      entries_[count_ - 1].sample = sample;
    } else {
      if (count_ == CAPACITY) removeOldest();
      entries_[count_++] = {nowMs, sample};
    }
    // Keep the point immediately before the window endpoint for interpolation.
    while (count_ > 1 && uint32_t(nowMs - entries_[1].atMs) >= WINDOW_MS) removeOldest();

    window_ = Window();
    score_ = 0;
    candidates_ = triggered_ = None;
    if (count_ < 2 || uint32_t(nowMs - entries_[0].atMs) < WINDOW_MS) {
      forwardConfirmation_ = Confirmation();
      verticalConfirmation_ = Confirmation();
      blendConfirmation_ = Confirmation();
      return false;
    }
    calculateWindow(nowMs);

    float forward = 0, vertical = 0;
    if (window_.speedValid && window_.positionValid)
      forward =
          std::max(0.0f, std::min(1.0f, std::min(window_.meanSpeedMps / FORWARD_SPEED_MPS,
                                                 window_.displacementM / FORWARD_DISTANCE_M)));
    const bool sameDirection = window_.altitudeValid && window_.climbValid &&
                               ((window_.altitudeChangeM > 0 && window_.meanClimbMps > 0) ||
                                (window_.altitudeChangeM < 0 && window_.meanClimbMps < 0));
    if (sameDirection)
      vertical = std::min(1.0f, std::min(std::abs(window_.altitudeChangeM) / VERTICAL_DISTANCE_M,
                                         std::abs(window_.meanClimbMps) / VERTICAL_CLIMB_MPS));
    // Saturating each fraction at one preserves all path decisions and keeps
    // diagnostic scores bounded even for extreme finite sensor inputs.
    const bool forwardCandidate = forward >= 1;
    const bool verticalCandidate = sameDirection &&
                                   std::abs(window_.altitudeChangeM) >= VERTICAL_DISTANCE_M &&
                                   std::abs(window_.meanClimbMps) > VERTICAL_CLIMB_MPS;
    const float blendScore = forward + vertical * vertical;
    const bool blendEligible = forward >= BLEND_MIN_FORWARD && vertical >= BLEND_MIN_VERTICAL;
    const bool blendCandidate = blendEligible && blendScore >= 1;
    if (forwardCandidate) candidates_ |= Forward;
    if (verticalCandidate) candidates_ |= Vertical;
    if (blendCandidate) candidates_ |= Blended;
    score_ = std::max(forward, vertical);
    if (blendEligible) score_ = std::max(score_, blendScore);

    if (forwardConfirmation_.update(nowMs, forwardCandidate, 0, 5000)) triggered_ |= Forward;
    const int8_t direction = window_.altitudeChangeM > 0 ? 1 : -1;
    if (verticalConfirmation_.update(nowMs, verticalCandidate, direction)) triggered_ |= Vertical;
    if (blendConfirmation_.update(nowMs, blendCandidate, direction)) triggered_ |= Blended;
    return triggered_ != None;
  }

 private:
  struct Entry {
    uint32_t atMs = 0;
    Sample sample;
  };
  struct Confirmation {
    uint32_t sinceMs = 0;
    bool pending = false;
    int8_t direction = 0;
    bool update(uint32_t nowMs, bool qualifies, int8_t sign = 0, uint32_t confirmMs = CONFIRM_MS) {
      if (!qualifies) {
        pending = false;
        return false;
      }
      if (!pending || direction != sign) {
        pending = true;
        sinceMs = nowMs;
        direction = sign;
      }
      return uint32_t(nowMs - sinceMs) >= confirmMs;
    }
  };

  void removeOldest() {
    for (size_t i = 1; i < count_; ++i) entries_[i - 1] = entries_[i];
    --count_;
  }
  static double interpolate(double a, double b, double fraction) { return a + (b - a) * fraction; }
  static double longitudeDelta(double a, double b) {
    double delta = b - a;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    return delta;
  }
  static float distance(double lat1, double lon1, double lat2, double lon2) {
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double halfLat = (lat2 - lat1) * radians / 2;
    const double halfLon = longitudeDelta(lon1, lon2) * radians / 2;
    const double a =
        std::sin(halfLat) * std::sin(halfLat) +
        std::cos(lat1 * radians) * std::cos(lat2 * radians) * std::sin(halfLon) * std::sin(halfLon);
    return static_cast<float>(12742000.0 * std::asin(std::sqrt(std::max(0.0, std::min(1.0, a)))));
  }

  void calculateWindow(uint32_t nowMs) {
    window_.speedValid = window_.climbValid = window_.altitudeValid = window_.positionValid = true;
    double speedArea = 0, climbArea = 0;
    for (size_t i = 0; i < count_; ++i) {
      const Sample& s = entries_[i].sample;
      window_.speedValid &= s.speedValid;
      window_.climbValid &= s.climbValid;
      window_.altitudeValid &= s.altitudeValid;
      window_.positionValid &= s.positionValid;
      if (!i) continue;
      const Sample& previous = entries_[i - 1].sample;
      const uint32_t leftAge = nowMs - entries_[i - 1].atMs;
      const uint32_t rightAge = nowMs - entries_[i].atMs;
      const uint32_t left = std::min(leftAge, WINDOW_MS);
      const double fraction = static_cast<double>(leftAge - left) / (leftAge - rightAge);
      const double seconds = (left - rightAge) / 1000.0;
      if (previous.speedValid && s.speedValid)
        speedArea +=
            (interpolate(previous.speedMps, s.speedMps, fraction) + s.speedMps) * seconds / 2;
      if (previous.climbValid && s.climbValid)
        climbArea +=
            (interpolate(previous.climbMps, s.climbMps, fraction) + s.climbMps) * seconds / 2;
    }
    if (window_.speedValid) window_.meanSpeedMps = speedArea / (WINDOW_MS / 1000.0);
    if (window_.climbValid) window_.meanClimbMps = climbArea / (WINDOW_MS / 1000.0);
    const Sample& a = entries_[0].sample;
    const Sample& b = entries_[1].sample;
    const Sample& last = entries_[count_ - 1].sample;
    const uint32_t ageA = nowMs - entries_[0].atMs;
    const uint32_t ageB = nowMs - entries_[1].atMs;
    const double fraction = static_cast<double>(ageA - WINDOW_MS) / (ageA - ageB);
    if (window_.altitudeValid)
      window_.altitudeChangeM = last.altitudeM - interpolate(a.altitudeM, b.altitudeM, fraction);
    if (window_.positionValid) {
      const double lat = interpolate(a.latitude, b.latitude, fraction);
      const double lon = a.longitude + longitudeDelta(a.longitude, b.longitude) * fraction;
      window_.displacementM = distance(lat, lon, last.latitude, last.longitude);
    }
    window_.speedValid &= std::isfinite(window_.meanSpeedMps);
    window_.climbValid &= std::isfinite(window_.meanClimbMps);
    window_.altitudeValid &= std::isfinite(window_.altitudeChangeM);
    window_.positionValid &= std::isfinite(window_.displacementM);
  }

  std::array<Entry, CAPACITY> entries_{};
  size_t count_ = 0;
  Window window_;
  float score_ = 0;
  uint8_t candidates_ = None, triggered_ = None;
  Confirmation forwardConfirmation_, verticalConfirmation_, blendConfirmation_;
};
