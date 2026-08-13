#include "navigation/thermal_tracker.h"

#include <math.h>
#include <string.h>

#include "instruments/baro.h"
#include "instruments/gps.h"
#include "navigation/gpx.h"

ThermalTracker thermalTracker;

namespace {
  int16_t wrap180(float deg) {
    while (deg > 180) deg -= 360;
    while (deg < -180) deg += 360;
    return static_cast<int16_t>(roundf(deg));
  }

  int16_t headingDelta(int16_t newer, int16_t older) { return wrap180(newer - older); }

  uint16_t absHeadingDelta(int16_t newer, int16_t older) {
    return static_cast<uint16_t>(abs(headingDelta(newer, older)));
  }

  uint8_t sampleIndex(uint8_t newestIndex, uint8_t offset) {
    return (newestIndex + ThermalTracker::MAX_DETECTOR_SAMPLES - offset) %
           ThermalTracker::MAX_DETECTOR_SAMPLES;
  }

  int16_t clampInt16(float value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(roundf(value));
  }

  int16_t clampInt16(double value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(round(value));
  }

  int16_t clampInt16(int32_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(value);
  }

  uint32_t distanceSq(int32_t dx, int32_t dy) {
    const int64_t x = dx;
    const int64_t y = dy;
    const int64_t result = x * x + y * y;
    return result > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(result);
  }
}  // namespace

void ThermalTracker::reset() {
  originValid_ = false;
  seededThisFlight_ = false;
  lastDetectorSecond_ = 0;
  lastLatE7_ = 0;
  lastLonE7_ = 0;
  nextSample_ = 0;
  sampleCount_ = 0;
  episodeStartAfterS_ = 0;
  episodeBoundaryValid_ = false;
  resetEpisode();
  displayItemCount_ = 0;
  memset(samples_, 0, sizeof(samples_));
  memset(thermals_, 0, sizeof(thermals_));
  memset(displayItems_, 0, sizeof(displayItems_));
}

void ThermalTracker::establishOrigin(double latitude, double longitude) {
  originLat_ = latitude;
  originLon_ = longitude;
  metersPerDegLon_ = METERS_PER_DEG_LAT * cos(latitude * DEG_TO_RAD);
  originValid_ = true;
}

bool ThermalTracker::readCurrentFix(Sample& sample) {
  GPSPositionSnapshot fix;
  if (!gps.lastValidFix(fix) || !baro.climbRateFilteredValid()) return false;
  if (!originValid_) establishOrigin(fix.latitude, fix.longitude);

  sample.valid = true;
  sample.xM = clampInt16((fix.longitude - originLon_) * metersPerDegLon_);
  sample.yM = clampInt16((fix.latitude - originLat_) * METERS_PER_DEG_LAT);
  sample.altM = static_cast<int16_t>(baro.altAdjusted() / 100);
  sample.courseDeg = static_cast<int16_t>(roundf(fix.courseDeg));
  sample.climb1SecCms = static_cast<int16_t>(constrain(
      baro.climbRate1SecAverageValid() ? baro.climbRate1SecAverage() : baro.climbRateFiltered(),
      -32768L, 32767L));
  sample.timeS = millis() / 1000;

  currentXM_ = sample.xM;
  currentYM_ = sample.yM;
  currentAltM_ = sample.altM;
  currentTrackDeg_ = sample.courseDeg;
  return true;
}

void ThermalTracker::readSeedReference(Sample& sample) {
  GPSPositionSnapshot fix;
  const bool hasFix = gps.lastValidFix(fix);
  const double latitude = hasFix ? fix.latitude : 0.0;
  const double longitude = hasFix ? fix.longitude : 0.0;
  if (!originValid_) establishOrigin(latitude, longitude);

  sample.valid = true;
  sample.xM = clampInt16((longitude - originLon_) * metersPerDegLon_);
  sample.yM = clampInt16((latitude - originLat_) * METERS_PER_DEG_LAT);
  sample.altM = static_cast<int16_t>(baro.altAdjusted() / 100);
  sample.courseDeg = hasFix ? static_cast<int16_t>(roundf(fix.courseDeg)) : 0;
  sample.climb1SecCms = static_cast<int16_t>(constrain(
      baro.climbRate1SecAverageValid() ? baro.climbRate1SecAverage() : baro.climbRateFiltered(),
      -32768L, 32767L));
  sample.timeS = millis() / 1000;

  currentXM_ = sample.xM;
  currentYM_ = sample.yM;
  currentAltM_ = sample.altM;
  currentTrackDeg_ = sample.courseDeg;
}

void ThermalTracker::updateDetector() {
  Sample sample;
  if (!readCurrentFix(sample)) return;

  const int32_t latE7 = gpxDegreesToE7(gps.location.lat());
  const int32_t lonE7 = gpxDegreesToE7(gps.location.lng());
  if (sample.timeS == lastDetectorSecond_ || (latE7 == lastLatE7_ && lonE7 == lastLonE7_)) {
    return;
  }
  lastDetectorSecond_ = sample.timeS;
  lastLatE7_ = latE7;
  lastLonE7_ = lonE7;

  addSample(sample);
  evaluateDetector();
}

uint8_t ThermalTracker::recentCoreSamples(CoreSample* out, uint8_t maxCount) const {
  if (out == nullptr || maxCount == 0) return 0;

  const uint8_t count = min(sampleCount_, maxCount);
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t offset = count - i;
    const Sample& sample = samples_[sampleIndex(nextSample_, offset)];
    out[i].valid = sample.valid;
    out[i].xM = sample.xM;
    out[i].yM = sample.yM;
    out[i].courseDeg = sample.courseDeg;
    out[i].climbCms = sample.climb1SecCms;
    out[i].timeS = sample.timeS;
  }
  return count;
}

void ThermalTracker::addSample(Sample sample) {
  for (uint8_t offset = 1; offset <= sampleCount_; ++offset) {
    const Sample& prior = samples_[sampleIndex(nextSample_, offset)];
    if (!prior.valid) continue;
    if (sample.timeS <= prior.timeS) continue;
    const uint32_t dt = sample.timeS - prior.timeS;
    if (dt > DETECTION_WINDOW_S) break;
    sample.climb30Cms = static_cast<int16_t>(
        constrain((static_cast<int32_t>(sample.altM - prior.altM) * 100) / static_cast<int32_t>(dt),
                  -32768L, 32767L));
  }

  samples_[nextSample_] = sample;
  nextSample_ = (nextSample_ + 1) % MAX_DETECTOR_SAMPLES;
  if (sampleCount_ < MAX_DETECTOR_SAMPLES) sampleCount_++;
}

void ThermalTracker::evaluateDetector() {
  if (sampleCount_ < 2) return;

  const uint8_t newestIndex = sampleIndex(nextSample_, 1);
  const Sample& newest = samples_[newestIndex];
  uint8_t offsets[MAX_DETECTOR_SAMPLES];
  const uint8_t offsetCount = collectCandidateWindow(offsets, MAX_DETECTOR_SAMPLES);
  int16_t windowGainM = 0;
  uint16_t windowTurnDeg = 0;
  const bool thermalWindow = windowLooksThermal(offsets, offsetCount, windowGainM, windowTurnDeg);

  if (thermalWindow) {
    if (!episode_.active) {
      uint8_t entryIndex = 0;
      for (uint8_t i = 1; i < offsetCount; ++i) {
        const Sample& sample = samples_[sampleIndex(nextSample_, offsets[i])];
        const Sample& entry = samples_[sampleIndex(nextSample_, offsets[entryIndex])];
        if (sample.altM < entry.altM) entryIndex = i;
      }
      resetEpisode();
      for (uint8_t i = entryIndex; i < offsetCount; ++i) {
        addEpisodePoint(samples_[sampleIndex(nextSample_, offsets[i])]);
      }
    } else {
      addEpisodePoint(newest);
    }
    return;
  }

  if (episode_.active) {
    evaluateEpisode(newest);
    episodeStartAfterS_ = newest.timeS;
    episodeBoundaryValid_ = true;
    resetEpisode();
  }
}

uint8_t ThermalTracker::collectCandidateWindow(uint8_t* offsets, uint8_t maxOffsets) const {
  if (sampleCount_ < 2 || maxOffsets == 0) return 0;

  const Sample& newest = samples_[sampleIndex(nextSample_, 1)];
  uint8_t count = 0;
  for (uint8_t offset = sampleCount_; offset >= 1; --offset) {
    const Sample& sample = samples_[sampleIndex(nextSample_, offset)];
    if (!sample.valid) continue;
    if (newest.timeS < sample.timeS || newest.timeS - sample.timeS > DETECTION_WINDOW_S) continue;
    if (episodeBoundaryValid_ && sample.timeS <= episodeStartAfterS_) continue;
    offsets[count++] = offset;
    if (count >= maxOffsets) break;
  }
  return count;
}

bool ThermalTracker::windowLooksThermal(const uint8_t* offsets, uint8_t count, int16_t& gainM,
                                        uint16_t& turnDeg) const {
  if (count < 2) return false;
  const Sample& oldest = samples_[sampleIndex(nextSample_, offsets[0])];
  const Sample& newest = samples_[sampleIndex(nextSample_, offsets[count - 1])];
  gainM = newest.altM - oldest.altM;
  turnDeg = longestDirectionalArc(offsets, count);
  return gainM >= CANDIDATE_GAIN_M && turnDeg >= CANDIDATE_TURN_DEG;
}

uint16_t ThermalTracker::longestDirectionalArc(const uint8_t* offsets, uint8_t count) const {
  int8_t direction = 0;
  uint16_t arc = 0;
  uint16_t reversalArc = 0;
  uint16_t longest = 0;
  int16_t lastBearing = 0;
  bool hasLastBearing = false;

  for (uint8_t i = 0; i < count; ++i) {
    const Sample& sample = samples_[sampleIndex(nextSample_, offsets[i])];
    if (!hasLastBearing) {
      lastBearing = sample.courseDeg;
      hasLastBearing = true;
      continue;
    }

    const int16_t delta = headingDelta(sample.courseDeg, lastBearing);
    lastBearing = sample.courseDeg;
    if (delta == 0) continue;

    const int8_t deltaDirection = delta > 0 ? 1 : -1;
    const uint16_t magnitude = static_cast<uint16_t>(abs(delta));
    if (direction == 0) {
      direction = deltaDirection;
      arc = magnitude;
      longest = max(longest, arc);
      continue;
    }
    if (deltaDirection == direction) {
      reversalArc = 0;
      arc += magnitude;
      longest = max(longest, arc);
      continue;
    }

    reversalArc += magnitude;
    if (reversalArc >= TURN_REVERSAL_HYSTERESIS_DEG) {
      longest = max(longest, arc);
      direction = deltaDirection;
      arc = reversalArc;
      reversalArc = 0;
      longest = max(longest, arc);
    }
  }
  return max(longest, arc);
}

void ThermalTracker::resetEpisode() { memset(&episode_, 0, sizeof(episode_)); }

void ThermalTracker::addEpisodePoint(const Sample& sample) {
  if (!episode_.active) {
    episode_.active = true;
    episode_.entry = sample;
    episode_.hasEntry = true;
  }
  if (!episode_.hasEntry) return;

  const int16_t climbWeightCms = max<int16_t>(-30, min<int16_t>(500, sample.climb30Cms));
  const uint8_t weight = static_cast<uint8_t>(max<int16_t>(2, climbWeightCms / 10 + 5));
  const int16_t gainFromEntryM = max<int16_t>(0, sample.altM - episode_.entry.altM);
  uint8_t bucketIndex = gainFromEntryM / SPINE_BUCKET_HEIGHT_M;
  if (bucketIndex >= MAX_THERMAL_NODES) bucketIndex = MAX_THERMAL_NODES - 1;
  SpineBucket& bucket = episode_.buckets[bucketIndex];
  bucket.weight += weight;
  bucket.sumX += static_cast<int32_t>(sample.xM) * weight;
  bucket.sumY += static_cast<int32_t>(sample.yM) * weight;
  bucket.sumAlt += static_cast<int32_t>(sample.altM) * weight;

  episode_.totalWeight += weight;
  episode_.sumX += static_cast<int32_t>(sample.xM) * weight;
  episode_.sumY += static_cast<int32_t>(sample.yM) * weight;
  episode_.sumAlt += static_cast<int32_t>(sample.altM) * weight;

  if (episode_.hasLastBearing)
    episode_.turnDeg += absHeadingDelta(sample.courseDeg, episode_.lastBearingDeg);
  episode_.lastBearingDeg = sample.courseDeg;
  episode_.hasLastBearing = true;

  if (!episode_.hasEntryCore) {
    if (episode_.hasEntryCoreLastBearing) {
      episode_.entryCoreTurnDeg +=
          absHeadingDelta(sample.courseDeg, episode_.entryCoreLastBearingDeg);
    }
    episode_.entryCoreLastBearingDeg = sample.courseDeg;
    episode_.hasEntryCoreLastBearing = true;
    if (episode_.entryCoreTurnDeg >= ENTRY_CORE_TURN_DEG) {
      episode_.entryCore = sample;
      episode_.hasEntryCore = true;
    }
  }

  if (!episode_.hasPeak || sample.altM >= episode_.peak.altM) {
    episode_.peak = sample;
    episode_.hasPeak = true;
    memcpy(episode_.peakBuckets, episode_.buckets, sizeof(episode_.peakBuckets));
    episode_.peakTotalWeight = episode_.totalWeight;
    episode_.peakSumX = episode_.sumX;
    episode_.peakSumY = episode_.sumY;
    episode_.peakSumAlt = episode_.sumAlt;
    episode_.peakTurnDeg = episode_.turnDeg;
  }
}

void ThermalTracker::evaluateEpisode(const Sample& closingSample) {
  (void)closingSample;
  if (!episode_.hasEntry || !episode_.hasPeak || episode_.peak.timeS <= episode_.entry.timeS)
    return;
  const uint16_t durationS = episode_.peak.timeS - episode_.entry.timeS;
  const int16_t gainM = episode_.peak.altM - episode_.entry.altM;
  if (durationS >= SAVE_DURATION_S && gainM >= SAVE_GAIN_M) saveEpisode(gainM, durationS);
}

void ThermalTracker::saveEpisode(int16_t gainM, uint16_t durationS) {
  ThermalNode nodes[MAX_THERMAL_NODES];
  uint8_t nodeCount = 0;
  buildSpineFromEpisode(nodes, nodeCount);
  if (nodeCount == 0) return;

  const int16_t avgClimbCms =
      durationS > 0 ? static_cast<int16_t>((static_cast<int32_t>(gainM) * 100) / durationS) : 0;
  const int8_t mergeTarget = findMergeTarget(nodes, nodeCount);
  if (mergeTarget >= 0) {
    mergeThermal(mergeTarget, nodes, nodeCount, gainM, avgClimbCms, durationS);
  } else {
    storeThermal(nodes, nodeCount, gainM, avgClimbCms, durationS);
  }
}

void ThermalTracker::buildSpineFromEpisode(ThermalNode* nodes, uint8_t& nodeCount) const {
  nodeCount = 0;
  if (!episode_.hasEntry || !episode_.hasPeak) return;

  for (uint8_t bucket = 0; bucket < MAX_THERMAL_NODES; ++bucket) {
    const SpineBucket& source = episode_.peakBuckets[bucket];
    if (source.weight <= 0) continue;
    nodes[nodeCount].xM = clampInt16(source.sumX / source.weight);
    nodes[nodeCount].yM = clampInt16(source.sumY / source.weight);
    nodes[nodeCount].altM = clampInt16(source.sumAlt / source.weight);
    nodeCount++;
  }

  if (nodeCount > 0 && episode_.hasEntryCore) {
    nodes[0].xM = episode_.entryCore.xM;
    nodes[0].yM = episode_.entryCore.yM;
    nodes[0].altM = episode_.entryCore.altM;
  }
  if (nodeCount == 1 && episode_.peak.timeS != episode_.entry.timeS) {
    const Sample& low = episode_.hasEntryCore ? episode_.entryCore : episode_.entry;
    nodes[0].xM = low.xM;
    nodes[0].yM = low.yM;
    nodes[0].altM = low.altM;
    nodes[1].xM = episode_.peak.xM;
    nodes[1].yM = episode_.peak.yM;
    nodes[1].altM = episode_.peak.altM;
    nodeCount = 2;
  }
}

int8_t ThermalTracker::findMergeTarget(const ThermalNode* nodes, uint8_t nodeCount) const {
  if (nodeCount == 0) return -1;

  const uint32_t centerRadiusSq =
      static_cast<uint32_t>(CENTER_MERGE_RADIUS_M) * CENTER_MERGE_RADIUS_M;
  const uint32_t splineRadiusSq =
      static_cast<uint32_t>(SPLINE_MERGE_RADIUS_M) * SPLINE_MERGE_RADIUS_M;
  for (uint8_t i = 0; i < MAX_SAVED_THERMALS; ++i) {
    const SavedThermal& thermal = thermals_[i];
    if (!thermal.valid || thermal.nodeCount == 0) continue;

    uint32_t splineDistanceSq = UINT32_MAX;
    if (sameAltitudeSpineDistanceSq(thermal, nodes, nodeCount, splineDistanceSq) &&
        splineDistanceSq <= splineRadiusSq)
      return i;
    if (extrapolatedSpineDistanceSq(thermal, nodes, nodeCount, splineDistanceSq) &&
        splineDistanceSq <= splineRadiusSq)
      return i;
    if (centerDistanceSq(thermal, nodes, nodeCount) <= centerRadiusSq) return i;
  }
  return -1;
}

uint32_t ThermalTracker::centerDistanceSq(const SavedThermal& thermal, const ThermalNode* nodes,
                                          uint8_t nodeCount) const {
  int32_t sumX = 0;
  int32_t sumY = 0;
  for (uint8_t i = 0; i < nodeCount; ++i) {
    sumX += nodes[i].xM;
    sumY += nodes[i].yM;
  }
  const int32_t x = sumX / max<uint8_t>(1, nodeCount);
  const int32_t y = sumY / max<uint8_t>(1, nodeCount);

  int32_t thermalX = 0;
  int32_t thermalY = 0;
  for (uint8_t n = 0; n < thermal.nodeCount; ++n) {
    thermalX += thermal.nodes[n].xM;
    thermalY += thermal.nodes[n].yM;
  }
  thermalX /= max<uint8_t>(1, thermal.nodeCount);
  thermalY /= max<uint8_t>(1, thermal.nodeCount);
  return distanceSq(thermalX - x, thermalY - y);
}

bool ThermalTracker::altitudeRange(const ThermalNode* nodes, uint8_t nodeCount, int16_t& minAlt,
                                   int16_t& maxAlt) const {
  if (nodeCount == 0) return false;
  minAlt = nodes[0].altM;
  maxAlt = nodes[0].altM;
  for (uint8_t i = 1; i < nodeCount; ++i) {
    minAlt = min(minAlt, nodes[i].altM);
    maxAlt = max(maxAlt, nodes[i].altM);
  }
  return true;
}

bool ThermalTracker::pointAtAltitude(const ThermalNode* nodes, uint8_t nodeCount, int16_t altM,
                                     uint16_t maxExtrapolationM, ThermalNode& point) const {
  if (nodeCount == 0) return false;
  if (nodeCount == 1) {
    if (abs(nodes[0].altM - altM) > maxExtrapolationM) return false;
    point = nodes[0];
    point.altM = altM;
    return true;
  }

  const ThermalNode* lower = &nodes[0];
  const ThermalNode* upper = &nodes[nodeCount - 1];
  if (altM < nodes[0].altM) {
    if (nodes[0].altM - altM > maxExtrapolationM) return false;
    lower = &nodes[0];
    upper = &nodes[1];
  } else if (altM > nodes[nodeCount - 1].altM) {
    if (altM - nodes[nodeCount - 1].altM > maxExtrapolationM) return false;
    lower = &nodes[nodeCount - 2];
    upper = &nodes[nodeCount - 1];
  } else {
    for (uint8_t i = 1; i < nodeCount; ++i) {
      if (altM <= nodes[i].altM) {
        lower = &nodes[i - 1];
        upper = &nodes[i];
        break;
      }
    }
  }

  if (lower->altM == upper->altM) {
    point = *lower;
    point.altM = altM;
    return true;
  }

  const int32_t dz = upper->altM - lower->altM;
  point.xM = clampInt16(lower->xM + ((static_cast<int32_t>(upper->xM - lower->xM) *
                                      static_cast<int32_t>(altM - lower->altM)) /
                                     dz));
  point.yM = clampInt16(lower->yM + ((static_cast<int32_t>(upper->yM - lower->yM) *
                                      static_cast<int32_t>(altM - lower->altM)) /
                                     dz));
  point.altM = altM;
  return true;
}

bool ThermalTracker::sameAltitudeSpineDistanceSq(const SavedThermal& thermal,
                                                 const ThermalNode* nodes, uint8_t nodeCount,
                                                 uint32_t& distanceSqOut) const {
  int16_t aMin = 0;
  int16_t aMax = 0;
  int16_t bMin = 0;
  int16_t bMax = 0;
  if (!altitudeRange(thermal.nodes, thermal.nodeCount, aMin, aMax) ||
      !altitudeRange(nodes, nodeCount, bMin, bMax))
    return false;

  const int16_t overlapMin = max(aMin, bMin);
  const int16_t overlapMax = min(aMax, bMax);
  if (overlapMin > overlapMax) return false;

  int16_t altitudes[3] = {overlapMin, static_cast<int16_t>((overlapMin + overlapMax) / 2),
                          overlapMax};
  bool found = false;
  distanceSqOut = UINT32_MAX;
  for (uint8_t i = 0; i < 3; ++i) {
    if (i > 0 && altitudes[i] == altitudes[i - 1]) continue;
    ThermalNode aPoint;
    ThermalNode bPoint;
    if (!pointAtAltitude(thermal.nodes, thermal.nodeCount, altitudes[i], 0, aPoint) ||
        !pointAtAltitude(nodes, nodeCount, altitudes[i], 0, bPoint))
      continue;
    distanceSqOut = min(distanceSqOut, distanceSq(aPoint.xM - bPoint.xM, aPoint.yM - bPoint.yM));
    found = true;
  }
  return found;
}

bool ThermalTracker::extrapolatedSpineDistanceSq(const SavedThermal& thermal,
                                                 const ThermalNode* nodes, uint8_t nodeCount,
                                                 uint32_t& distanceSqOut) const {
  int16_t aMin = 0;
  int16_t aMax = 0;
  int16_t bMin = 0;
  int16_t bMax = 0;
  if (!altitudeRange(thermal.nodes, thermal.nodeCount, aMin, aMax) ||
      !altitudeRange(nodes, nodeCount, bMin, bMax))
    return false;
  if (max(aMin, bMin) <= min(aMax, bMax)) return false;

  const ThermalNode* lowerNodes = aMax < bMin ? thermal.nodes : nodes;
  const uint8_t lowerCount = aMax < bMin ? thermal.nodeCount : nodeCount;
  const ThermalNode* upperNodes = aMax < bMin ? nodes : thermal.nodes;
  const uint8_t upperCount = aMax < bMin ? nodeCount : thermal.nodeCount;
  const int16_t lowerMax = aMax < bMin ? aMax : bMax;
  const int16_t upperMin = aMax < bMin ? bMin : aMin;
  const int16_t gap = upperMin - lowerMax;
  if (gap < 0 || gap > MAX_ALTITUDE_EXTRAPOLATION_M) return false;

  const int16_t altitudes[3] = {static_cast<int16_t>(lowerMax + gap / 2), lowerMax, upperMin};
  const uint16_t lowerGaps[3] = {MAX_ALTITUDE_EXTRAPOLATION_M, 0, MAX_ALTITUDE_EXTRAPOLATION_M};
  const uint16_t upperGaps[3] = {MAX_ALTITUDE_EXTRAPOLATION_M, MAX_ALTITUDE_EXTRAPOLATION_M, 0};

  bool found = false;
  distanceSqOut = UINT32_MAX;
  for (uint8_t i = 0; i < 3; ++i) {
    ThermalNode lowerPoint;
    ThermalNode upperPoint;
    if (!pointAtAltitude(lowerNodes, lowerCount, altitudes[i], lowerGaps[i], lowerPoint) ||
        !pointAtAltitude(upperNodes, upperCount, altitudes[i], upperGaps[i], upperPoint))
      continue;
    distanceSqOut = min(distanceSqOut,
                        distanceSq(lowerPoint.xM - upperPoint.xM, lowerPoint.yM - upperPoint.yM));
    found = true;
  }
  return found;
}

void ThermalTracker::mergeThermal(uint8_t target, const ThermalNode* nodes, uint8_t nodeCount,
                                  int16_t gainM, int16_t avgClimbCms, uint16_t durationS) {
  SavedThermal& thermal = thermals_[target];
  const uint16_t oldDuration = max<uint16_t>(1, thermal.durationS);
  const uint16_t newDuration = max<uint16_t>(1, durationS);
  const uint16_t totalDuration = oldDuration + newDuration;

  const uint8_t count = min<uint8_t>(thermal.nodeCount, nodeCount);
  for (uint8_t i = 0; i < count; ++i) {
    thermal.nodes[i].xM =
        (thermal.nodes[i].xM * oldDuration + nodes[i].xM * newDuration) / totalDuration;
    thermal.nodes[i].yM =
        (thermal.nodes[i].yM * oldDuration + nodes[i].yM * newDuration) / totalDuration;
    thermal.nodes[i].altM =
        (thermal.nodes[i].altM * oldDuration + nodes[i].altM * newDuration) / totalDuration;
  }
  for (uint8_t i = count; i < nodeCount && i < MAX_THERMAL_NODES; ++i) {
    thermal.nodes[i] = nodes[i];
    thermal.nodeCount++;
  }
  thermal.gainM += gainM;
  thermal.durationS += durationS;
  thermal.avgClimbCms =
      (thermal.avgClimbCms * oldDuration + avgClimbCms * newDuration) / totalDuration;
  thermal.lastSeenS = millis() / 1000;
  thermal.seeded = false;
}

void ThermalTracker::storeThermal(const ThermalNode* nodes, uint8_t nodeCount, int16_t gainM,
                                  int16_t avgClimbCms, uint16_t durationS) {
  SavedThermal& thermal = thermals_[replacementIndex()];
  thermal.valid = true;
  thermal.seeded = false;
  thermal.nodeCount = min<uint8_t>(nodeCount, MAX_THERMAL_NODES);
  for (uint8_t i = 0; i < thermal.nodeCount; ++i) thermal.nodes[i] = nodes[i];
  thermal.gainM = gainM;
  thermal.avgClimbCms = avgClimbCms;
  thermal.durationS = durationS;
  thermal.lastSeenS = millis() / 1000;
}

uint8_t ThermalTracker::replacementIndex() const {
  for (uint8_t i = 0; i < MAX_SAVED_THERMALS; ++i) {
    if (!thermals_[i].valid) return i;
  }

  uint8_t weakest = 0;
  int32_t weakestScore = INT32_MAX;
  for (uint8_t i = 0; i < MAX_SAVED_THERMALS; ++i) {
    const SavedThermal& thermal = thermals_[i];
    int32_t score = thermal.gainM + thermal.avgClimbCms + thermal.durationS / 2;
    if (thermal.seeded) score -= 10000;
    if (score < weakestScore) {
      weakestScore = score;
      weakest = i;
    }
  }
  return weakest;
}

ThermalNode ThermalTracker::pointAtAltitude(const SavedThermal& thermal, int16_t altM,
                                            bool& nearAltitude) const {
  nearAltitude = false;
  if (thermal.nodeCount == 0) return ThermalNode();
  if (thermal.nodeCount == 1) {
    nearAltitude = abs(thermal.nodes[0].altM - altM) <= 250;
    return thermal.nodes[0];
  }

  uint8_t lower = 0;
  uint8_t upper = thermal.nodeCount - 1;
  for (uint8_t i = 1; i < thermal.nodeCount; ++i) {
    if (thermal.nodes[i].altM <= altM) lower = i;
    if (thermal.nodes[i].altM >= altM) {
      upper = i;
      break;
    }
  }
  const ThermalNode& a = thermal.nodes[lower];
  const ThermalNode& b = thermal.nodes[upper];
  nearAltitude = altM >= thermal.nodes[0].altM - 100 &&
                 altM <= thermal.nodes[thermal.nodeCount - 1].altM + 100;
  if (a.altM == b.altM) return a;

  const float t = static_cast<float>(altM - a.altM) / static_cast<float>(b.altM - a.altM);
  ThermalNode result;
  result.xM = clampInt16(a.xM + (b.xM - a.xM) * t);
  result.yM = clampInt16(a.yM + (b.yM - a.yM) * t);
  result.altM = altM;
  return result;
}

uint8_t ThermalTracker::qualityFor(const SavedThermal& thermal, bool nearAltitude) const {
  uint8_t q = 1;
  if (thermal.avgClimbCms >= 120) q++;
  if (thermal.avgClimbCms >= 240) q++;
  if (thermal.gainM >= 250) q++;
  if (nearAltitude) q++;
  return constrain(q, 1, 5);
}

uint16_t ThermalTracker::mapDistanceToRadius(int16_t distanceM, bool& clamped) const {
  clamped = distanceM > 5000;
  if (distanceM <= 500) return map(distanceM, 0, 500, 0, 25);
  if (distanceM <= 1500) return map(distanceM, 500, 1500, 25, 37);
  if (distanceM <= 5000) return map(distanceM, 1500, 5000, 37, 48);
  return 48;
}

void ThermalTracker::updateNavigation() {
  Sample current;
  if (!readCurrentFix(current)) return;

  rebuildDisplayItems();
}

void ThermalTracker::rebuildDisplayItems() {
  displayItemCount_ = 0;
  int8_t selected = -1;
  uint16_t selectedAbsTurn = 361;

  for (uint8_t i = 0; i < MAX_SAVED_THERMALS; ++i) {
    const SavedThermal& thermal = thermals_[i];
    if (!thermal.valid || displayItemCount_ >= MAX_THERMAL_DISPLAY_ITEMS) continue;

    bool nearAltitude = false;
    const ThermalNode point = pointAtAltitude(thermal, currentAltM_, nearAltitude);
    const int16_t dx = point.xM - currentXM_;
    const int16_t dy = point.yM - currentYM_;
    const int16_t distanceM = static_cast<int16_t>(min<float>(32767, hypot(dx, dy)));
    const int16_t bearingDeg = static_cast<int16_t>(roundf(atan2(dx, dy) * RAD_TO_DEG));
    const int16_t turnDeg = wrap180(bearingDeg - currentTrackDeg_);
    bool clamped = false;
    const uint16_t radius = mapDistanceToRadius(distanceM, clamped);

    ThermalDisplayItem& item = displayItems_[displayItemCount_];
    item.valid = true;
    item.index = i;
    item.distanceM = distanceM;
    item.turnDeg = turnDeg;
    item.avgClimbCms = thermal.avgClimbCms;
    item.gainM = thermal.gainM;
    item.quality = qualityFor(thermal, nearAltitude);
    item.selected = false;
    item.clamped = clamped;
    item.nearAltitude = nearAltitude;
    item.xOffset = static_cast<int8_t>(roundf(sin(turnDeg * DEG_TO_RAD) * radius));
    item.yOffset = static_cast<int8_t>(roundf(-cos(turnDeg * DEG_TO_RAD) * radius));

    const uint16_t absTurn = abs(turnDeg);
    if (absTurn < selectedAbsTurn) {
      selectedAbsTurn = absTurn;
      selected = displayItemCount_;
    }
    displayItemCount_++;
  }

  if (selected >= 0) displayItems_[selected].selected = true;
  sortDisplayItems();
}

void ThermalTracker::sortDisplayItems() {
  for (uint8_t i = 0; i < displayItemCount_; ++i) {
    for (uint8_t j = i + 1; j < displayItemCount_; ++j) {
      if (displayItems_[j].quality > displayItems_[i].quality ||
          (displayItems_[j].selected && !displayItems_[i].selected)) {
        ThermalDisplayItem tmp = displayItems_[i];
        displayItems_[i] = displayItems_[j];
        displayItems_[j] = tmp;
      }
    }
  }
}

const ThermalDisplayItem* ThermalTracker::selectedDisplayItem() const {
  for (uint8_t i = 0; i < displayItemCount_; ++i) {
    if (displayItems_[i].valid && displayItems_[i].selected) return &displayItems_[i];
  }
  return nullptr;
}

uint8_t ThermalTracker::savedThermalCount() const {
  uint8_t count = 0;
  for (const auto& thermal : thermals_) {
    if (thermal.valid) count++;
  }
  return count;
}

void ThermalTracker::seedTestThermalsForFlight() {
  if (seededThisFlight_) return;

  Sample current;
  readSeedReference(current);
  memset(thermals_, 0, sizeof(thermals_));

  const int16_t bearingsDeg[MAX_SAVED_THERMALS] = {-6,  18,   -38, 55,  -72, 96,  -118, 145,
                                                   178, -165, 28,  -24, 72,  -88, 122};
  const uint16_t distancesM[MAX_SAVED_THERMALS] = {420,  780,  1100, 1450, 1800, 2300, 2700, 3200,
                                                   3800, 4400, 5200, 5600, 6100, 6600, 7000};
  const uint8_t targetQualities[MAX_SAVED_THERMALS] = {1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 1, 2, 3, 4, 5};

  for (uint8_t i = 0; i < MAX_SAVED_THERMALS; ++i) {
    SavedThermal& thermal = thermals_[i];
    thermal.valid = true;
    thermal.seeded = true;
    thermal.nodeCount = MAX_THERMAL_NODES;
    thermal.avgClimbCms = targetQualities[i] >= 4 ? 260 : targetQualities[i] == 3 ? 125 : 80;
    thermal.gainM = targetQualities[i] == 5 ? 315 : 90;
    thermal.durationS = 60 + (i % 5) * 50;
    thermal.lastSeenS = millis() / 1000;

    const float absoluteBearing = currentTrackDeg_ + bearingsDeg[i];
    const int16_t baseX = currentXM_ + sin(absoluteBearing * DEG_TO_RAD) * distancesM[i];
    const int16_t baseY = currentYM_ + cos(absoluteBearing * DEG_TO_RAD) * distancesM[i];
    const int16_t baseAltitudeOffsetM = targetQualities[i] == 1 ? 600 : -180 + (i % 4) * 80;
    for (uint8_t n = 0; n < MAX_THERMAL_NODES; ++n) {
      thermal.nodes[n].xM = baseX + (static_cast<int8_t>(n) - 1) * (8 + i % 4);
      thermal.nodes[n].yM = baseY + n * (10 + i % 5);
      thermal.nodes[n].altM = currentAltM_ + baseAltitudeOffsetM + n * 120;
    }
  }
  seededThisFlight_ = true;
  rebuildDisplayItems();
}
