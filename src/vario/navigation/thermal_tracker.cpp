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

  uint8_t sampleIndex(uint8_t newestIndex, uint8_t offset) {
    return (newestIndex + ThermalTracker::MAX_DETECTOR_SAMPLES - offset) %
           ThermalTracker::MAX_DETECTOR_SAMPLES;
  }

  int16_t clampInt16(float value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(roundf(value));
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
  candidateActive_ = false;
  candidateStartS_ = 0;
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

void ThermalTracker::addSample(const Sample& sample) {
  samples_[nextSample_] = sample;
  nextSample_ = (nextSample_ + 1) % MAX_DETECTOR_SAMPLES;
  if (sampleCount_ < MAX_DETECTOR_SAMPLES) sampleCount_++;
}

void ThermalTracker::evaluateDetector() {
  if (sampleCount_ < 2) return;

  const uint8_t newestIndex = sampleIndex(nextSample_, 1);
  const Sample& newest = samples_[newestIndex];
  uint8_t oldestOffset = 0;
  for (uint8_t offset = 0; offset < sampleCount_; ++offset) {
    const Sample& candidate = samples_[sampleIndex(nextSample_, offset + 1)];
    if (!candidate.valid || newest.timeS - candidate.timeS > DETECTION_WINDOW_S) break;
    oldestOffset = offset + 1;
  }
  if (oldestOffset < 2) return;

  const Sample& oldest = samples_[sampleIndex(nextSample_, oldestOffset)];
  const int16_t gainM = newest.altM - oldest.altM;
  int16_t turnDeg = 0;
  for (uint8_t offset = oldestOffset; offset > 1; --offset) {
    const Sample& a = samples_[sampleIndex(nextSample_, offset)];
    const Sample& b = samples_[sampleIndex(nextSample_, offset - 1)];
    turnDeg += abs(headingDelta(b.courseDeg, a.courseDeg));
  }
  const bool windowLooksThermal = gainM >= CANDIDATE_GAIN_M && turnDeg >= CANDIDATE_TURN_DEG;

  if (windowLooksThermal) {
    if (!candidateActive_) {
      candidateActive_ = true;
      candidateStartS_ = oldest.timeS;
    }
    return;
  }

  if (!candidateActive_) return;

  uint8_t startOffset = 1;
  uint8_t endOffset = 1;
  for (uint8_t offset = 1; offset <= sampleCount_; ++offset) {
    const Sample& sample = samples_[sampleIndex(nextSample_, offset)];
    if (!sample.valid || sample.timeS < candidateStartS_) break;
    startOffset = offset;
  }
  const Sample& start = samples_[sampleIndex(nextSample_, startOffset)];
  const Sample& end = samples_[sampleIndex(nextSample_, endOffset)];
  const uint16_t durationS = end.timeS > start.timeS ? end.timeS - start.timeS : 0;
  const int16_t episodeGainM = end.altM - start.altM;
  if (durationS >= SAVE_DURATION_S && episodeGainM >= SAVE_GAIN_M) {
    saveCandidate(startOffset, endOffset, episodeGainM, durationS);
  }
  candidateActive_ = false;
}

void ThermalTracker::saveCandidate(uint8_t startOffset, uint8_t endOffset, int16_t gainM,
                                   uint16_t durationS) {
  ThermalNode nodes[MAX_THERMAL_NODES];
  uint8_t nodeCount = 0;
  buildSpine(startOffset, endOffset, nodes, nodeCount);
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

void ThermalTracker::buildSpine(uint8_t startOffset, uint8_t endOffset, ThermalNode* nodes,
                                uint8_t& nodeCount) {
  nodeCount = 0;
  if (startOffset < endOffset) return;

  const Sample& start = samples_[sampleIndex(nextSample_, startOffset)];
  const Sample& end = samples_[sampleIndex(nextSample_, endOffset)];
  int16_t minAlt = min(start.altM, end.altM);
  int16_t maxAlt = max(start.altM, end.altM);
  for (uint8_t offset = endOffset; offset <= startOffset; ++offset) {
    const Sample& sample = samples_[sampleIndex(nextSample_, offset)];
    minAlt = min(minAlt, sample.altM);
    maxAlt = max(maxAlt, sample.altM);
  }

  nodeCount = MAX_THERMAL_NODES;
  if (maxAlt - minAlt < 60) nodeCount = 2;
  int32_t sumX[MAX_THERMAL_NODES] = {};
  int32_t sumY[MAX_THERMAL_NODES] = {};
  int32_t sumAlt[MAX_THERMAL_NODES] = {};
  uint8_t counts[MAX_THERMAL_NODES] = {};
  const int16_t span = max<int16_t>(1, maxAlt - minAlt);

  for (uint8_t offset = endOffset; offset <= startOffset; ++offset) {
    const Sample& sample = samples_[sampleIndex(nextSample_, offset)];
    uint8_t bucket = ((sample.altM - minAlt) * nodeCount) / (span + 1);
    if (bucket >= nodeCount) bucket = nodeCount - 1;
    sumX[bucket] += sample.xM;
    sumY[bucket] += sample.yM;
    sumAlt[bucket] += sample.altM;
    counts[bucket]++;
  }

  uint8_t written = 0;
  for (uint8_t bucket = 0; bucket < nodeCount; ++bucket) {
    if (counts[bucket] == 0) continue;
    nodes[written].xM = sumX[bucket] / counts[bucket];
    nodes[written].yM = sumY[bucket] / counts[bucket];
    nodes[written].altM = sumAlt[bucket] / counts[bucket];
    written++;
  }
  nodeCount = written;
}

int8_t ThermalTracker::findMergeTarget(const ThermalNode* nodes, uint8_t nodeCount) const {
  if (nodeCount == 0) return -1;

  int32_t sumX = 0;
  int32_t sumY = 0;
  for (uint8_t i = 0; i < nodeCount; ++i) {
    sumX += nodes[i].xM;
    sumY += nodes[i].yM;
  }
  const int16_t x = sumX / nodeCount;
  const int16_t y = sumY / nodeCount;

  for (uint8_t i = 0; i < MAX_SAVED_THERMALS; ++i) {
    const SavedThermal& thermal = thermals_[i];
    if (!thermal.valid || thermal.nodeCount == 0) continue;
    int32_t thermalX = 0;
    int32_t thermalY = 0;
    for (uint8_t n = 0; n < thermal.nodeCount; ++n) {
      thermalX += thermal.nodes[n].xM;
      thermalY += thermal.nodes[n].yM;
    }
    thermalX /= thermal.nodeCount;
    thermalY /= thermal.nodeCount;
    if (hypot(static_cast<float>(thermalX - x), static_cast<float>(thermalY - y)) <=
        MERGE_RADIUS_M) {
      return i;
    }
  }
  return -1;
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
  nearAltitude = altM >= thermal.nodes[0].altM - 250 &&
                 altM <= thermal.nodes[thermal.nodeCount - 1].altM + 250;
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
  if (thermal.avgClimbCms >= 100) q++;
  if (thermal.avgClimbCms >= 220) q++;
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
