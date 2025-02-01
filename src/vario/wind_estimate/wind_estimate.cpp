
#include "wind_estimate.h"
#include <limits>
#include "log.h"

bool DEBUG_WIND_ESTIMATE = false;	// enable for verbose serial printing

WindEstimate windEstimate;

TotalSamples totalSamples;

struct WindEstimateAdjustment {
  // Change in easterly component of windspeed, m/s
  float dwx;

  // Change in northerly component of windspeed, m/s
  float dwy;

  // Change in aircraft airspeed, m/s
  float dairspeed;
};

const int adjustmentCount = 6;
const WindEstimateAdjustment adjustments[adjustmentCount] = {
    WindEstimateAdjustment{.dwx = 0.1f, .dwy = 0, .dairspeed = 0},
    WindEstimateAdjustment{.dwx = -0.1f, .dwy = 0, .dairspeed = 0},
    WindEstimateAdjustment{.dwx = 0, .dwy = 0.1f, .dairspeed = 0},
    WindEstimateAdjustment{.dwx = 0, .dwy = -0.1f, .dairspeed = 0},
    WindEstimateAdjustment{.dwx = 0, .dwy = 0, .dairspeed = 0.1f},
    WindEstimateAdjustment{.dwx = 0, .dwy = 0, .dairspeed = -0.1f},
};

WindEstimate getWindEstimate(void) {
  return windEstimate;
}

void windEstimate_onNewSentence(NMEASentenceContents contents) {
  if (contents.course || contents.speed) {
    GroundVelocity v = {.trackAngle = DEG_TO_RAD * gps.course.deg(), .speed = gps.speed.mps()};

    if (getAreWeFlying()) submitVelocityForWindEstimate(v);
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

inline float dxOf(float angle, float speed) {
  return cos(angle) * speed;
}

inline float dyOf(float angle, float speed) {
  return sin(angle) * speed;
}

// convert angle and speed into Dx Dy points for circle-fitting
bool onlyConvertAverage = false;
void convertToDxDy() {
  for (int b = 0; b < binCount; b++) {
    // convert average angle/speed into average dx/dy
    totalSamples.bin[b].averageDx =
        dxOf(totalSamples.bin[b].averageAngle, totalSamples.bin[b].averageSpeed);
    totalSamples.bin[b].averageDy =
        dyOf(totalSamples.bin[b].averageAngle, totalSamples.bin[b].averageSpeed);

    // convert all angle/speed into all dx/dy
    if (!onlyConvertAverage) {
      for (int s = 0; s < totalSamples.bin[b].sampleCount; s++) {
        totalSamples.bin[b].dx[s] =
            dxOf(totalSamples.bin[b].angle[s], totalSamples.bin[b].speed[s]);
        totalSamples.bin[b].dy[s] =
            dyOf(totalSamples.bin[b].angle[s], totalSamples.bin[b].speed[s]);
      }
    }
  }
}

// check if we have at least 3 bins with points, and
// that the bins span at least a semi circle
bool checkIfEnoughPoints() {
  bool enough = false;  // assume we don't have enough

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
  if (populatedBinCount >= populatedBinsRequired && populatedSpan >= binsRequiredForSemiCircle) {
    enough = true;
  }

  return enough;
}

// Compute the error of the given wind estimate.
//   wx: Windspeed in the easterly direction, m/s
//   wy: Windspeed in the northerly direction, m/s
//   airspeed: Constant airspeed of aircraft, m/s
float errorOf(float wx, float wy, float airspeed) {
  float sumSquareError = 0;
  int n = 0;
  for (int bin = 0; bin < binCount; bin++) {
    for (int sample = 0; sample < totalSamples.bin[bin].sampleCount; sample++) {
      float dx = totalSamples.bin[bin].dx[sample] - wx;
      float dy = totalSamples.bin[bin].dy[sample] - wy;
      float dr = sqrt(dx * dx + dy * dy) - airspeed;
      sumSquareError += dr * dr;
      n++;
    }
  }
  return sqrt(sumSquareError / n);
}

inline float speedOf(float wx, float wy) {
  return sqrt(wx * wx + wy * wy);
}

inline float directionOf(float wx, float wy) {
  return atan2(wy, wx);	// atan2 takes y, x in cpp/arduino, unlike other languages and tools which take x, y
}

// Perform work toward updating the wind estimate.
// Returns: true if estimate update is complete, false if still in progress.
int updateCount = 0;
int betterCount = 0;
bool updateEstimate() {
  updateCount++;
  float wx = dxOf(windEstimate.windDirectionTrue, windEstimate.windSpeed);
  float wy = dyOf(windEstimate.windDirectionTrue, windEstimate.windSpeed);
  float bestError = errorOf(wx, wy, windEstimate.airspeed);

	if (DEBUG_WIND_ESTIMATE) {
		Serial.print("Begin Estimate!  Wx: ");
		Serial.print(wx);
		Serial.print(" wy: ");
		Serial.print(wy);		
		Serial.print(" Dir: ");
		Serial.print(windEstimate.windDirectionTrue);
		Serial.print(" Spd: ");
		Serial.print(windEstimate.windSpeed);
		Serial.print(" Airspd: ");
		Serial.print(windEstimate.airspeed);
		Serial.print(" Err: ");
		Serial.println(bestError);
	}

  int bestAdjustment = -1;

  // TODO: split each adjustment into a separate invocation of updateEstimate if needed for
  // responsitivity (or remove this TODO if already responsive enough)
  for (int a = 0; a < adjustmentCount; a++) {
    float newError = errorOf(wx + adjustments[a].dwx,
                             wy + adjustments[a].dwy,
                             windEstimate.airspeed + adjustments[a].dairspeed);

		if (DEBUG_WIND_ESTIMATE) {
      Serial.print("bestError: ");
      Serial.print(bestError, 4);
      Serial.print(" newError: ");
      Serial.println(newError, 4);
		}

    if (newError < bestError) {

      bestError = newError;
      bestAdjustment = a;
    }
  }

  if (bestAdjustment >= 0) {
    betterCount++;    
    // New estimate is available
    windEstimate.airspeed += adjustments[bestAdjustment].dairspeed;
    wx += adjustments[bestAdjustment].dwx;
    wy += adjustments[bestAdjustment].dwy;
    windEstimate.windSpeed = speedOf(wx, wy);
    windEstimate.windDirectionTrue = directionOf(wx, wy);
		windEstimate.windDirectionFrom = windEstimate.windDirectionTrue + PI;	// add 180 degrees
    windEstimate.error = bestError;

		if (DEBUG_WIND_ESTIMATE) {
			Serial.print("UPDATE ESTIMATE! Wx: ");
			Serial.print(wx);
			Serial.print(" wy: ");
			Serial.print(wy);		
			Serial.print(" Dir: ");
			Serial.print(windEstimate.windDirectionTrue);
			Serial.print(" Spd: ");
			Serial.print(windEstimate.windSpeed);
			Serial.print(" Airspd: ");
			Serial.print(windEstimate.airspeed);
			Serial.print(" Err: ");
			Serial.println(windEstimate.error);
		}

		return true;
  } else {
    // Current estimate cannot be improved upon
  }
  windEstimate.validEstimate = true;

  return true;
}

// temp testing values
float tempWindDir = 0;
float tempWindSpeed = 0;
int8_t dir = 1;
bool enoughPoints = false;

int getUpdateCount() {
  return updateCount;
}
int getBetterCount() {
  return betterCount;
}
int getBinCount() {
	return binCount;
}

uint8_t windEstimateStep = 0;
void estimateWind() {
	int estimateTimeStamp = micros();
  switch (windEstimateStep) {
    case 0:
      convertToDxDy();			
			if (DEBUG_WIND_ESTIMATE) {
				Serial.print("**TIME** convert to Dx Dy: ");
				Serial.println(micros() - estimateTimeStamp);
				estimateTimeStamp = micros();
			}
      enoughPoints = checkIfEnoughPoints();
      if (enoughPoints) {
        averageSamplePoints();
				if (DEBUG_WIND_ESTIMATE) {
					Serial.print("**TIME** enough and average points: ");
					Serial.println(micros() - estimateTimeStamp);
				}
        windEstimateStep++;
      }
      break;
    case 1:
      if (updateEstimate()) windEstimateStep = 0;
			if (DEBUG_WIND_ESTIMATE) {
				Serial.print("**TIME** update estimate: ");
				Serial.println(micros() - estimateTimeStamp);
			}
      break;
  }
}

void clearWindEstimate(void) {
  windEstimate.validEstimate = false;
  windEstimate.windSpeed = 0;
  windEstimate.windDirectionTrue = 0;
  windEstimate.airspeed = STANDARD_AIRSPEED;
  windEstimate.error = std::numeric_limits<float>::max();
}

// ingest a sample groundVelocity and store it in the appropriate bin
void submitVelocityForWindEstimate(GroundVelocity groundVelocity) {
  
	// get GroundVelocity track relative to current wind estimate (if Valid)
	// (the center of the pie moves with the wind, and we want 
	// bins relative to the center of the pie)
	float relativeAngle = groundVelocity.trackAngle; // start with existing track angle

	if (windEstimate.validEstimate) {									// adjust for wind if estimate is valid
		float dx = cos(groundVelocity.trackAngle);
		float dy = sin(groundVelocity.trackAngle);
		float wx = dxOf(windEstimate.windDirectionTrue, windEstimate.windSpeed);
		float wy = dyOf(windEstimate.windDirectionTrue, windEstimate.windSpeed);
		relativeAngle = directionOf(dx - wx, dy - wy);
	}
		
	// stay positive, since we'll search bins from 0 to 2*PI
	if (relativeAngle < 0) {
		relativeAngle += 2 * PI;
	}
		
	// now sort into appropriate bin
  float binAngleSpan = 2 * PI / binCount;
  for (int b = 0; b < binCount; b++) {
    if (relativeAngle < (b + 1) * binAngleSpan) {
      totalSamples.bin[b].angle[totalSamples.bin[b].index] = relativeAngle;
      totalSamples.bin[b].speed[totalSamples.bin[b].index] = groundVelocity.speed;
      totalSamples.bin[b].index++;
      totalSamples.bin[b].sampleCount++;
      if (totalSamples.bin[b].sampleCount > samplesPerBin) {
        totalSamples.bin[b].sampleCount = samplesPerBin;
      }
      if (totalSamples.bin[b].index >= samplesPerBin) {
        totalSamples.bin[b].index = 0;
      }
      break;
    }
  }
}