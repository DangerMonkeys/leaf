#pragma once

#include <Arduino.h>

constexpr uint8_t MAX_SAVED_THERMALS = 15;
constexpr uint8_t MAX_THERMAL_NODES = 4;
constexpr uint8_t MAX_THERMAL_DISPLAY_ITEMS = MAX_SAVED_THERMALS;

struct ThermalNode {
  int16_t xM = 0;
  int16_t yM = 0;
  int16_t altM = 0;
};

struct SavedThermal {
  bool valid = false;
  bool seeded = false;
  uint8_t nodeCount = 0;
  ThermalNode nodes[MAX_THERMAL_NODES];
  int16_t avgClimbCms = 0;
  int16_t gainM = 0;
  uint16_t durationS = 0;
  uint32_t lastSeenS = 0;
};

struct ThermalDisplayItem {
  bool valid = false;
  uint8_t index = 0;
  int16_t distanceM = 0;
  int16_t turnDeg = 0;
  int16_t avgClimbCms = 0;
  int16_t gainM = 0;
  uint8_t quality = 0;
  bool selected = false;
  bool clamped = false;
  bool nearAltitude = false;
  int8_t xOffset = 0;
  int8_t yOffset = 0;
};

class ThermalTracker {
 public:
  static constexpr uint8_t MAX_DETECTOR_SAMPLES = 40;

  void reset();
  void updateDetector();
  void updateNavigation();
  void seedTestThermalsForFlight();

  const ThermalDisplayItem* displayItems() const { return displayItems_; }
  uint8_t displayItemCount() const { return displayItemCount_; }
  const ThermalDisplayItem* selectedDisplayItem() const;
  uint8_t savedThermalCount() const;

 private:
  struct Sample {
    bool valid = false;
    int16_t xM = 0;
    int16_t yM = 0;
    int16_t altM = 0;
    int16_t courseDeg = 0;
    int16_t climb30Cms = 0;
    uint32_t timeS = 0;
  };

  struct SpineBucket {
    int32_t weight = 0;
    int32_t sumX = 0;
    int32_t sumY = 0;
    int32_t sumAlt = 0;
  };

  struct EpisodeState {
    bool active = false;
    bool hasEntry = false;
    bool hasPeak = false;
    bool hasEntryCore = false;
    Sample entry;
    Sample peak;
    Sample entryCore;
    SpineBucket buckets[MAX_THERMAL_NODES];
    SpineBucket peakBuckets[MAX_THERMAL_NODES];
    int32_t totalWeight = 0;
    int32_t sumX = 0;
    int32_t sumY = 0;
    int32_t sumAlt = 0;
    int32_t peakTotalWeight = 0;
    int32_t peakSumX = 0;
    int32_t peakSumY = 0;
    int32_t peakSumAlt = 0;
    int16_t lastBearingDeg = 0;
    int16_t entryCoreLastBearingDeg = 0;
    uint16_t turnDeg = 0;
    uint16_t peakTurnDeg = 0;
    uint16_t entryCoreTurnDeg = 0;
    bool hasLastBearing = false;
    bool hasEntryCoreLastBearing = false;
  };

  static constexpr uint16_t DETECTION_WINDOW_S = 30;
  static constexpr int16_t CANDIDATE_GAIN_M = 10;
  static constexpr int16_t CANDIDATE_TURN_DEG = 180;
  static constexpr int16_t TURN_REVERSAL_HYSTERESIS_DEG = 30;
  static constexpr uint16_t SAVE_DURATION_S = 25;
  static constexpr int16_t SAVE_GAIN_M = 65;
  static constexpr uint16_t CENTER_MERGE_RADIUS_M = 200;
  static constexpr uint16_t SPLINE_MERGE_RADIUS_M = 100;
  static constexpr uint16_t MAX_ALTITUDE_EXTRAPOLATION_M = 250;
  static constexpr uint16_t SPINE_BUCKET_HEIGHT_M = 200;
  static constexpr uint16_t ENTRY_CORE_TURN_DEG = 180;
  static constexpr double METERS_PER_DEG_LAT = 111320.0;

  bool readCurrentFix(Sample& sample);
  void readSeedReference(Sample& sample);
  void establishOrigin(double latitude, double longitude);
  void addSample(Sample sample);
  void evaluateDetector();
  uint8_t collectCandidateWindow(uint8_t* offsets, uint8_t maxOffsets) const;
  bool windowLooksThermal(const uint8_t* offsets, uint8_t count, int16_t& gainM,
                          uint16_t& turnDeg) const;
  uint16_t longestDirectionalArc(const uint8_t* offsets, uint8_t count) const;
  void resetEpisode();
  void addEpisodePoint(const Sample& sample);
  void evaluateEpisode(const Sample& closingSample);
  void saveEpisode(int16_t gainM, uint16_t durationS);
  void buildSpineFromEpisode(ThermalNode* nodes, uint8_t& nodeCount) const;
  int8_t findMergeTarget(const ThermalNode* nodes, uint8_t nodeCount) const;
  uint32_t centerDistanceSq(const SavedThermal& thermal, const ThermalNode* nodes,
                            uint8_t nodeCount) const;
  bool altitudeRange(const ThermalNode* nodes, uint8_t nodeCount, int16_t& minAlt,
                     int16_t& maxAlt) const;
  bool pointAtAltitude(const ThermalNode* nodes, uint8_t nodeCount, int16_t altM,
                       uint16_t maxExtrapolationM, ThermalNode& point) const;
  bool sameAltitudeSpineDistanceSq(const SavedThermal& thermal, const ThermalNode* nodes,
                                   uint8_t nodeCount, uint32_t& distanceSq) const;
  bool extrapolatedSpineDistanceSq(const SavedThermal& thermal, const ThermalNode* nodes,
                                   uint8_t nodeCount, uint32_t& distanceSq) const;
  void mergeThermal(uint8_t target, const ThermalNode* nodes, uint8_t nodeCount, int16_t gainM,
                    int16_t avgClimbCms, uint16_t durationS);
  void storeThermal(const ThermalNode* nodes, uint8_t nodeCount, int16_t gainM, int16_t avgClimbCms,
                    uint16_t durationS);
  uint8_t replacementIndex() const;
  ThermalNode pointAtAltitude(const SavedThermal& thermal, int16_t altM, bool& nearAltitude) const;
  uint8_t qualityFor(const SavedThermal& thermal, bool nearAltitude) const;
  uint16_t mapDistanceToRadius(int16_t distanceM, bool& clamped) const;
  void rebuildDisplayItems();
  void sortDisplayItems();

  bool originValid_ = false;
  double originLat_ = 0;
  double originLon_ = 0;
  double metersPerDegLon_ = 0;
  int16_t currentXM_ = 0;
  int16_t currentYM_ = 0;
  int16_t currentAltM_ = 0;
  int16_t currentTrackDeg_ = 0;
  uint32_t lastDetectorSecond_ = 0;
  int32_t lastLatE7_ = 0;
  int32_t lastLonE7_ = 0;
  bool seededThisFlight_ = false;

  Sample samples_[MAX_DETECTOR_SAMPLES];
  uint8_t nextSample_ = 0;
  uint8_t sampleCount_ = 0;
  uint32_t episodeStartAfterS_ = 0;
  bool episodeBoundaryValid_ = false;
  EpisodeState episode_;

  SavedThermal thermals_[MAX_SAVED_THERMALS];
  ThermalDisplayItem displayItems_[MAX_THERMAL_DISPLAY_ITEMS];
  uint8_t displayItemCount_ = 0;
};

extern ThermalTracker thermalTracker;
