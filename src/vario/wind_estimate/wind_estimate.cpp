
#include "wind_estimate.h"

struct WindEstimate windEstimate;

struct TotalSamples totalSamples;

WindEstimate getWindEstimate(void) {
	return windEstimate;
}

void windEstimate_onNewSentence(NMEASentenceContents contents) {

	if (contents.course || contents.speed) {
		const float DEG2RAD = 0.01745329251f;
		GroundVelocity v = {
			.trackAngle = DEG2RAD * gps.course.deg(), 
			.speed = gps.speed.mps()
			};
		
		submitVelocityForWindEstimate(v);
	}
}

void averageSamplePoints() {
	for (int i = 0; i < binCount; i++) {
		float sumAngle = 0;
		float sumSpeed = 0;
		for (int j = 0; j < totalSamples.bin[i].sampleCount; j++) {
			sumAngle += totalSamples.bin[i].angle[j];
			sumSpeed += totalSamples.bin[i].speed[j];
		}
		totalSamples.bin[i].averageAngle = sumAngle / totalSamples.bin[i].sampleCount;
		totalSamples.bin[i].averageSpeed = sumSpeed / totalSamples.bin[i].sampleCount;
	}
}

// convert angle and speed into Dx Dy points for circle-fitting
bool justConvertAverage = true;
void convertToDxDy() {
	if (justConvertAverage) {
		for (int i = 0; i < binCount; i++) {
			totalSamples.bin[i].averageDx = cos(totalSamples.bin[i].averageAngle) * totalSamples.bin[i].averageSpeed;
			totalSamples.bin[i].averageDy = sin(totalSamples.bin[i].averageAngle) * totalSamples.bin[i].averageSpeed;
		}
	} else {
		for (int i = 0; i < binCount; i++) {
			for (int j = 0; j < totalSamples.bin[i].sampleCount; j++) {
				totalSamples.bin[i].dx[j] = cos(totalSamples.bin[i].angle[j]) * totalSamples.bin[i].speed[j];
				totalSamples.bin[i].dy[j] = sin(totalSamples.bin[i].angle[j]) * totalSamples.bin[i].speed[j];
			}
		}
	}
}

// check if we have at least 3 bins with points, and
// that the bins span at least a semi circle
bool checkIfEnoughPoints() {
	bool enough = false;	// assume we don't have enough
	
	uint8_t populatedBinCount = 0;
	const uint8_t populatedBinsRequired = 3;
	uint8_t continuousEmptyBinCount = 0;
	const uint8_t binsRequiredForSemiCircle = binCount / 2;

	uint8_t firstBinToHavePoints = 0;
	bool haveAStartingBin = false;
	uint8_t populatedSpan = 0;

	for (int i = 0; i < binCount; i++) {
		if (totalSamples.bin[i].sampleCount > 0) {
			populatedBinCount++;
			continuousEmptyBinCount = 0;
			if (!haveAStartingBin) {
				firstBinToHavePoints = i;
				haveAStartingBin = true;
			} else {
				populatedSpan = i - firstBinToHavePoints;				
			}
		} else {
			continuousEmptyBinCount++;
			// if we span a half a circle with no points, we can't have 
			// more than half a circle with points, so return false now
			if (continuousEmptyBinCount >= binsRequiredForSemiCircle) {
				return false;
			}
		}
	}

	// if we have enough bins and they span more than a semi circle, we have enough
	if (populatedBinCount >= populatedBinsRequired &&
			populatedSpan >= binsRequiredForSemiCircle) {
		enough = true;
	}

	return enough;
}

bool findEstimate() {
	// do circle fitting

	// Ben's magic goes here


	// if finished, populate the estimate and return true	
	if (1) {


		return true;

	// otherwise return false and this function will be called again so we can continue calculating
	} else {	
		return false;
	}
}


// temp testing values
float tempWindDir = 0;
float tempWindSpeed = 0;
int8_t dir = 1;


uint8_t windEstimateStep = 0;
void estimateWind() {

	// Temporary Values for Testing			
		windEstimate.windDirectionTrue = tempWindDir;
		windEstimate.windSpeed = tempWindSpeed;
		windEstimate.validEstimate = true;

		tempWindDir += 0.02;
		if (tempWindDir > 2 * PI) tempWindDir = 0;
		tempWindSpeed += (dir * 0.2);
		if (tempWindSpeed > 25) dir = -1;
		if (tempWindSpeed < 0) dir = 1;

		return;


	switch (windEstimateStep) {
		case 0:
			averageSamplePoints();			
			windEstimateStep++;
			break;
		case 1:
			convertToDxDy();
			windEstimateStep++;
			break;
		case 2:
			if (checkIfEnoughPoints()) windEstimateStep++;
			break;
		case 3:
			if (findEstimate()) windEstimateStep = 0;
			break;
	}	
}


// ingest a sample groundVelocity and store it in the appropriate bin
void submitVelocityForWindEstimate(GroundVelocity groundVelocity) {

	// sort into appropriate bin
	float binAngleSpan = 2 * PI / binCount;
	for (int i = 0; i < binCount; i++) {
		if (groundVelocity.trackAngle < i * binAngleSpan) {			
			totalSamples.bin[i].angle[totalSamples.bin[i].index] = groundVelocity.trackAngle;
			totalSamples.bin[i].speed[totalSamples.bin[i].index] = groundVelocity.speed;
			totalSamples.bin[i].index++;
			totalSamples.bin[i].sampleCount++;
			if (totalSamples.bin[i].sampleCount > samplesPerBin) {
				totalSamples.bin[i].sampleCount = samplesPerBin;
			}
			if (totalSamples.bin[i].index >= samplesPerBin) {
				totalSamples.bin[i].index = 0;
			}	
			break;
		}
	}
}