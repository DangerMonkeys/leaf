#pragma once

#include "navigation/thermal_center.h"

// Store only two completed, non-overlapping lap summaries, not another breadcrumb buffer.
class ThermalCoreClimbHistory {
 public:
  void reset() { *this = {}; }

  void add(const ThermalLiftCenter& lap) {
    if (!lap.valid || lap.lapDurationMs <= 0 ||
        (count_ && static_cast<int32_t>(lap.lapStartMs - lastEndMs_) < 0))
      return;
    integral_[1] = integral_[0];
    duration_[1] = duration_[0];
    integral_[0] = lap.climbIntegralCmsMs;
    duration_[0] = lap.lapDurationMs;
    lastEndMs_ = lap.lapEndMs;
    if (count_ < 2) ++count_;
  }

  float averageCms() const {
    const float duration = duration_[0] + duration_[1];
    return duration > 0 ? (integral_[0] + integral_[1]) / duration : 0;
  }

 private:
  float integral_[2] = {};
  float duration_[2] = {};
  uint32_t lastEndMs_ = 0;
  uint8_t count_ = 0;
};

// A retained target has its own lifetime, independent of whether the current path can fit a lap.
class ThermalCoreTarget {
 public:
  void reset() { *this = {}; }
  bool hasCenter() const { return valid_; }
  bool visible() const { return valid_ && distanceVisible_; }
  float eastM() const { return eastM_; }
  float northM() const { return northM_; }
  float averageClimbCms() const { return climb_.averageCms(); }

  void projectToAltitude(float altitudeM) {
    if (valid_) {
      eastM_ += slopeEast_ * (altitudeM - altitudeM_);
      northM_ += slopeNorth_ * (altitudeM - altitudeM_);
    }
    if (pending_) {
      candidateEastM_ += candidateSlopeEast_ * (altitudeM - altitudeM_);
      candidateNorthM_ += candidateSlopeNorth_ * (altitudeM - altitudeM_);
    }
    altitudeM_ = altitudeM;
  }

  void observe(const ThermalLiftCenter& lap, uint32_t sampleMs) {
    if (observed_ && sampleMs == lastObservationMs_) return;
    const bool continuous = observed_ && sampleMs - lastObservationMs_ <= 2000;
    observed_ = true;
    lastObservationMs_ = sampleMs;
    if (!continuous) clearCandidate();
    if (!lap.valid) {
      clearCandidate();
      return;
    }
    if (!valid_) {
      eastM_ = lap.eastM;
      northM_ = lap.northM;
      slopeEast_ = lap.slopeEast;
      slopeNorth_ = lap.slopeNorth;
      climb_.add(lap);
      valid_ = distanceVisible_ = true;
      return;
    }

    const float separationSquared = squaredDistance(eastM_, northM_, lap.eastM, lap.northM);
    if (separationSquared > 40.0f * 40.0f) {
      if (!pending_ || squaredDistance(candidateEastM_, candidateNorthM_, lap.eastM, lap.northM) >
                           15.0f * 15.0f) {
        clearCandidate();
        pending_ = true;
        candidateSinceMs_ = sampleMs;
        candidateEastM_ = lap.eastM;
        candidateNorthM_ = lap.northM;
        candidateSlopeEast_ = lap.slopeEast;
        candidateSlopeNorth_ = lap.slopeNorth;
      }
      candidateClimb_.add(lap);
      // Compare before accepting any of this candidate's climb into the old target's history.
      if (candidateClimb_.averageCms() > climb_.averageCms()) {
        if (sampleMs - candidateSinceMs_ < 3000) return;
        eastM_ = lap.eastM;
        northM_ = lap.northM;
        slopeEast_ = lap.slopeEast;
        slopeNorth_ = lap.slopeNorth;
        climb_ = candidateClimb_;
        clearCandidate();
        return;
      }
    } else {
      clearCandidate();
    }

    // Nearby, equal-strength and weaker candidates retain the existing 35% position smoothing.
    eastM_ += (lap.eastM - eastM_) * 0.35f;
    northM_ += (lap.northM - northM_) * 0.35f;
    slopeEast_ = lap.slopeEast;
    slopeNorth_ = lap.slopeNorth;
    climb_.add(lap);
  }

  void updateVisibility(float pilotEastM, float pilotNorthM, uint32_t sampleMs, bool fresh) {
    if (!fresh) {
      outside_ = false;
      distanceObserved_ = false;
      clearCandidate();
      return;
    }
    if (!valid_) return;
    if (distanceObserved_ && sampleMs == lastDistanceSampleMs_) return;
    if (!distanceObserved_ || sampleMs - lastDistanceSampleMs_ > 2000) outside_ = false;
    distanceObserved_ = true;
    lastDistanceSampleMs_ = sampleMs;
    if (squaredDistance(eastM_, northM_, pilotEastM, pilotNorthM) <= 100.0f * 100.0f) {
      outside_ = false;
      distanceVisible_ = true;
    } else {
      if (!outside_) {
        outside_ = true;
        outsideSinceMs_ = sampleMs;
      }
      if (sampleMs - outsideSinceMs_ >= 5000) distanceVisible_ = false;
    }
  }

 private:
  static float squaredDistance(float x1, float y1, float x2, float y2) {
    const float dx = x2 - x1, dy = y2 - y1;
    return dx * dx + dy * dy;
  }
  void clearCandidate() {
    pending_ = false;
    candidateClimb_.reset();
  }

  ThermalCoreClimbHistory climb_;
  ThermalCoreClimbHistory candidateClimb_;
  float eastM_ = 0, northM_ = 0, altitudeM_ = 0;
  float slopeEast_ = 0, slopeNorth_ = 0;
  float candidateEastM_ = 0, candidateNorthM_ = 0;
  float candidateSlopeEast_ = 0, candidateSlopeNorth_ = 0;
  uint32_t lastObservationMs_ = 0, candidateSinceMs_ = 0;
  uint32_t lastDistanceSampleMs_ = 0, outsideSinceMs_ = 0;
  bool valid_ = false, distanceVisible_ = false, observed_ = false;
  bool pending_ = false, outside_ = false, distanceObserved_ = false;
};
