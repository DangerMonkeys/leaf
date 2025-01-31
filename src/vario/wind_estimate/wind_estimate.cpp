
#include "wind_estimate.h"

struct WindEstimate windEstimate;

struct TotalSamples totalSamples;

WindEstimate getWindEstimate(void) {
	return windEstimate;
}

void windEstimate_onNewSentence(NMEASentenceContents contents) {

	if (contents.course || contents.speed) {
		GroundVelocity v = {
			.trackAngle = DEG_TO_RAD * gps.course.deg(), 
			.speed = gps.speed.mps()
			};
		
		submitVelocityForWindEstimate(v);
	}
}

void averageSamplePoints() {
	for (int b = 0; b < binCount; b++) {
		float sumAngle = 0;
		float sumSpeed = 0;
		for (int s = 0; s < totalSamples.bin[b].sampleCount; s++) {
			sumAngle += totalSamples.bin[b].angle[s];
			sumSpeed += totalSamples.bin[b].speed[s];
		}
		totalSamples.bin[b].averageAngle = sumAngle / totalSamples.bin[b].sampleCount;
		totalSamples.bin[b].averageSpeed = sumSpeed / totalSamples.bin[b].sampleCount;
	}
}

// convert angle and speed into Dx Dy points for circle-fitting
bool justConvertAverage = true;
void convertToDxDy() {
	if (justConvertAverage) {
		for (int b = 0; b < binCount; b++) {
			totalSamples.bin[b].averageDx = cos(totalSamples.bin[b].averageAngle) * totalSamples.bin[b].averageSpeed;
			totalSamples.bin[b].averageDy = sin(totalSamples.bin[b].averageAngle) * totalSamples.bin[b].averageSpeed;
		}
	} else {
		for (int b = 0; b < binCount; b++) {
			for (int s = 0; s < totalSamples.bin[b].sampleCount; s++) {
				totalSamples.bin[b].dx[s] = cos(totalSamples.bin[b].angle[s]) * totalSamples.bin[b].speed[s];
				totalSamples.bin[b].dy[s] = sin(totalSamples.bin[b].angle[s]) * totalSamples.bin[b].speed[s];
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

	for (int b = 0; b < binCount; b++) {
		if (totalSamples.bin[b].sampleCount > 0) {
			populatedBinCount++;
			continuousEmptyBinCount = 0;
			if (!haveAStartingBin) {
				firstBinToHavePoints = b;
				haveAStartingBin = true;
			} else {
				populatedSpan = b - firstBinToHavePoints;				
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
			if (checkIfEnoughPoints()) windEstimateStep++;			
			break;
		case 1:
			averageSamplePoints();			
			windEstimateStep++;
			break;
		case 2:
			convertToDxDy();
			windEstimateStep++;
			break;
		case 3:
			if (findEstimate()) windEstimateStep = 0;
			break;
	}	
}


float calculateErrorOfEstimate(WindEstimate candidateEstimate) {
	// calculate the error of the estimate
	float error = 0;
	for (int b = 0; b < binCount; b++) {
		for (int s = 0; s < totalSamples.bin[b].sampleCount; s++) {
			float dx = cos(totalSamples.bin[b].angle[s]) * totalSamples.bin[b].speed[s] - candidateEstimate.windSpeed * cos(candidateEstimate.windDirectionTrue);
			float dy = sin(totalSamples.bin[b].angle[s]) * totalSamples.bin[b].speed[s] - candidateEstimate.windSpeed * sin(candidateEstimate.windDirectionTrue);
			error += sqrt(dx*dx + dy*dy);
		}
	}
	return error;
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