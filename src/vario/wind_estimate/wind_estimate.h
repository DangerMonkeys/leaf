#pragma once

#include "gps.h"



// bin definitions for storing sample points
const uint8_t binCount = 12;
const uint8_t samplesPerBin = 6;

// the "pie slice" bucket for storing samples
struct Bin {
	float angle[samplesPerBin];	// radians East of North (track angle over the ground)
	float speed[samplesPerBin]; // m/s ground speed
	float averageAngle;
	float averageSpeed;
	float dx[samplesPerBin];
	float dy[samplesPerBin];
	float averageDx;
	float averageDy;
	uint8_t index;	// the wrap-around bookmark for where to add new values
	uint8_t sampleCount; // track how many in case the bin isn't full yet
};

// the "full pie" of all samples to use for	wind estimation
struct TotalSamples {
	Bin bin[binCount];
};

struct WindEstimate {
	// m/s windspeed estimate
	float windSpeed;	
	// Radians East of North (0 is True North)
	float windDirectionTrue;
	// m/s airspeed estimate (airspeed value pops out of the circle fitting algorithm)
	float airspeed;
	// flag if the estimate is valid or not (to be used in UI display functions, etc)
	bool validEstimate = false;
};

struct GroundVelocity{
	float trackAngle;
	float speed;
};

void windEstimate_onNewSentence(NMEASentenceContents contents);
void submitVelocityForWindEstimate(GroundVelocity groundVelocity);
float calculateErrorOfEstimate(WindEstimate candidateEstimate);

WindEstimate getWindEstimate(void);

// call frequently, each invocation should take no longer than 10ms
// each invocation will advance the wind estimation process
void estimateWind(void);


