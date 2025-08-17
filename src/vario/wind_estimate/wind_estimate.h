#pragma once

#include "instruments/gps.h"

// bin definitions for storing sample points
constexpr uint8_t BIN_COUNT = 8;
constexpr uint8_t SAMPLES_PER_BIN = 6;

// 10 m/s typical airspeed used as a starting point for wind estimate
constexpr float STANDARD_AIRSPEED = 10;

// the "full pie" of all samples to use for	wind estimation
struct TotalSamples {
  // the "pie slice" bucket for storing samples
  struct Bin {
    float angle[SAMPLES_PER_BIN];  // radians East of North (track angle over the ground)
    float speed[SAMPLES_PER_BIN];  // m/s ground speed
    float averageAngle;
    float averageSpeed;
    float dx[SAMPLES_PER_BIN];
    float dy[SAMPLES_PER_BIN];
    float averageDx;
    float averageDy;
    uint8_t index;        // the wrap-around bookmark for where to add new values
    uint8_t sampleCount;  // track how many in case the bin isn't full yet
  };

  Bin bin[BIN_COUNT];
};

struct WindEstimate {
  // m/s estimate of wind speed
  float windSpeed;

  // Radians East of North (0 is True North)
  float windDirectionTrue;  // direction wind is blowing toward (used for center of circle-fit)
  float windDirectionFrom;  // direction wind is blowing FROM (180 deg offset from previous var)
  // "from" is the typical/standard way of reporting wind direction, but "to" is helpful for
  // calculations

  // m/s estimate of average airspeed used in circle-fit
  float airspeed;

  // m/s estimate of current aircraft airspeed, using latest GPS speed and wind estimate
  float airspeedLive;

  // error of estimate
  float error;

  // most recent bin to receive a sample point (used mostly for debugging)
  int8_t recentBin = -1;

  // flag if estimate currently holds a valid estimate or not
  bool validEstimate = false;
};

class WindEstimator : public etl::message_router<WindEstimator, GpsReading> {
 public:
  void subscribe(etl::imessage_bus* bus) { bus->subscribe(*this); }

  // etl::message_router<WindEstimator, GpsReading>
  void on_receive(const GpsReading& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

  // call frequently, each invocation should take no longer than 10ms
  // each invocation will advance the wind estimation process
  void estimateWind();

  const WindEstimate& getWindEstimate() const { return windEstimate_; }
  int binCount() const { return BIN_COUNT; }

  // == for testing and debugging ==

  // increment each time a wind estimate update completes
  int updateCount() const { return updateCount_; }
  // increment each time a wind estimate update yields a better solution
  int betterCount() const { return betterCount_; }
  const TotalSamples& totalSamples() const { return totalSamples_; }

  void clearWindEstimate();

 private:
  struct GroundVelocity {
    float trackAngle;  // radians east from North.  Must be positive.
    float speed;
  };

  // ingest a sample groundVelocity and store it in the appropriate bin
  void submitVelocityForWindEstimate(GroundVelocity groundVelocity);

  void averageSamplePoints();

  // Perform work toward updating the wind estimate.
  // Returns: true if estimate update is complete, false if still in progress.
  bool updateEstimate();

  // convert angle and speed into Dx Dy points for circle-fitting
  void convertToDxDy();

  // check if we have at least 3 bins with points, and
  // that the bins span at least a semi circle
  bool checkIfEnoughPoints();

  // Compute the error of the given wind estimate.
  //   wx: Windspeed in the easterly direction, m/s
  //   wy: Windspeed in the northerly direction, m/s
  //   airspeed: Constant airspeed of aircraft, m/s
  float errorOf(float wx, float wy, float airspeed) const;

  uint8_t windEstimateStep_ = 0;

  WindEstimate windEstimate_;

  TotalSamples totalSamples_;

  int updateCount_ = 0;
  int betterCount_ = 0;
};

extern WindEstimator windEstimator;
