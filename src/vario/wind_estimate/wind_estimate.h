#pragma once

#include "gps.h"


struct WindEstimate {
	// m/s speed estimate
	float windSpeed;	

	// Radians East of North (0 is True North)
	float windDirectionTrue;

	bool validEstimate = false;
};

struct GroundVelocity{
	float trackAngle;
	float speed;
};

void windEstimate_onNewSentence(NMEASentenceContents contents);

void submitVelocityForWindEstimate(GroundVelocity groundVelocity);

WindEstimate getWindEstimate(void);

// call frequently, each invocation should take no longer than 10ms
void estimateWind(void);


