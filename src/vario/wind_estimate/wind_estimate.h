#pragma once

#include "gps.h"

// bin definitions for storing sample points
const uint8_t binCount = 8;
const uint8_t samplesPerBin = 6;

const float STANDARD_AIRSPEED =
    10;  // 10 m/s typical airspeed used as a starting point for wind estimate

// the "pie slice" bucket for storing samples
struct Bin {
  float angle[samplesPerBin];  // radians East of North (track angle over the ground)
  float speed[samplesPerBin];  // m/s ground speed
  float averageAngle;
  float averageSpeed;
  float dx[samplesPerBin];
  float dy[samplesPerBin];
  float averageDx;
  float averageDy;
  uint8_t index;        // the wrap-around bookmark for where to add new values
  uint8_t sampleCount;  // track how many in case the bin isn't full yet
};

// the "full pie" of all samples to use for	wind estimation
struct TotalSamples {
  Bin bin[binCount];
};
extern struct TotalSamples totalSamples;

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

struct GroundVelocity {
  float trackAngle;  // radians east from North.  Must be positive.
  float speed;
};

void windEstimate_onNewSentence(NMEASentenceContents contents);

void submitVelocityForWindEstimate(GroundVelocity groundVelocity);

WindEstimate getWindEstimate(void);
int getBinCount(void);

// for testing and debugging
int getUpdateCount(void);  // increment each time a wind estimate update completes
int getBetterCount(void);  // increment each time a wind estimate update yields a better solution

void clearWindEstimate(void);

// call frequently, each invocation should take no longer than 10ms
// each invocation will advance the wind estimation process
void estimateWind(void);
