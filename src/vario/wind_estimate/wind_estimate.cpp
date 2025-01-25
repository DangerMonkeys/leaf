
#include "wind_estimate.h"

struct WindEstimate windEstimate;

WindEstimate getWindEstimate(void) {
	return windEstimate;
}

void windEstimate_onNewSentence(NMEASentenceContents contents) {

	if (contents.course || contents.speed) {
		const float DEG2RAD = 0.01745329251f;
		GroundVelocity v = {.trackAngle = DEG2RAD * gps.course.deg(), .speed = gps.speed.mps()};
		
		submitVelocityForWindEstimate(v);
	}
}


void estimateWind() {

	// set proper values from the estimate
	windEstimate.windDirectionTrue = 1.2;
	windEstimate.windSpeed = 8.2;
	windEstimate.validEstimate = true;
}

void submitVelocityForWindEstimate(GroundVelocity groundVelocity) {

}