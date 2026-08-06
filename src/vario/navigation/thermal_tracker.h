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
    uint32_t timeS = 0;
  };

  static constexpr uint16_t DETECTION_WINDOW_S = 30;
  static constexpr int16_t CANDIDATE_GAIN_M = 10;
  static constexpr int16_t CANDIDATE_TURN_DEG = 60;
  static constexpr uint16_t SAVE_DURATION_S = 25;
  static constexpr int16_t SAVE_GAIN_M = 65;
  static constexpr uint16_t MERGE_RADIUS_M = 225;
  static constexpr double METERS_PER_DEG_LAT = 111320.0;

  bool readCurrentFix(Sample& sample);
  void readSeedReference(Sample& sample);
  void establishOrigin(double latitude, double longitude);
  void addSample(const Sample& sample);
  void evaluateDetector();
  void saveCandidate(uint8_t startOffset, uint8_t endOffset, int16_t gainM, uint16_t durationS);
  void buildSpine(uint8_t startOffset, uint8_t endOffset, ThermalNode* nodes, uint8_t& nodeCount);
  int8_t findMergeTarget(const ThermalNode* nodes, uint8_t nodeCount) const;
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
  bool candidateActive_ = false;
  uint32_t candidateStartS_ = 0;

  SavedThermal thermals_[MAX_SAVED_THERMALS];
  ThermalDisplayItem displayItems_[MAX_THERMAL_DISPLAY_ITEMS];
  uint8_t displayItemCount_ = 0;
};

extern ThermalTracker thermalTracker;
