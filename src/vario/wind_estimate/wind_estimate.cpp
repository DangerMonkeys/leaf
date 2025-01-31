
#include "wind_estimate.h"
#include <limits>

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
    const float DEG2RAD = 0.01745329251f;
    GroundVelocity v = {.trackAngle = DEG2RAD * gps.course.deg(), .speed = gps.speed.mps()};

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

inline float dxOf(float angle, float speed) {
  return cos(angle) * speed;
}

inline float dyOf(float angle, float speed) {
  return sin(angle) * speed;
}

// convert angle and speed into Dx Dy points for circle-fitting
bool justConvertAverage = true;
void convertToDxDy() {
  if (justConvertAverage) {
    for (int i = 0; i < binCount; i++) {
      totalSamples.bin[i].averageDx =
          dxOf(totalSamples.bin[i].averageAngle, totalSamples.bin[i].averageSpeed);
      totalSamples.bin[i].averageDy =
          dyOf(totalSamples.bin[i].averageAngle, totalSamples.bin[i].averageSpeed);
    }
  } else {
    for (int i = 0; i < binCount; i++) {
      for (int j = 0; j < totalSamples.bin[i].sampleCount; j++) {
        totalSamples.bin[i].dx[j] =
            dxOf(totalSamples.bin[i].angle[j], totalSamples.bin[i].speed[j]);
        totalSamples.bin[i].dy[j] =
            dyOf(totalSamples.bin[i].angle[j], totalSamples.bin[i].speed[j]);
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
  return atan2(wx, wy);
}

// Perform work toward updating the wind estimate.
// Returns: true if estimate update is complete, false if still in progress.
bool updateEstimate() {
  float wx = dxOf(windEstimate.windDirectionTrue, windEstimate.windDirectionTrue);
  float wy = dyOf(windEstimate.windDirectionTrue, windEstimate.windDirectionTrue);
  float bestError = errorOf(wx, wy, windEstimate.airspeed);
  int bestAdjustment = -1;

  // TODO: split each adjustment into a separate invocation of updateEstimate if needed for
  // responsitivity (or remove this TODO if already responsive enough)
  for (int a = 0; a < adjustmentCount; a++) {
    float newError = errorOf(wx + adjustments[a].dwx,
                             wy + adjustments[a].dwy,
                             windEstimate.airspeed + adjustments[a].dairspeed);
    if (newError < bestError) {
      bestError = newError;
      bestAdjustment = a;
    }
  }

  if (bestAdjustment >= 0) {
    // New estimate is available
    windEstimate.airspeed += adjustments[bestAdjustment].dairspeed;
    wx += adjustments[bestAdjustment].dwx;
    wy += adjustments[bestAdjustment].dwy;
    windEstimate.windSpeed = speedOf(wx, wy);
    windEstimate.windDirectionTrue = directionOf(wx, wy);
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

uint8_t windEstimateStep = 0;
void estimateWind() {
  switch (windEstimateStep) {
    case 0:
      if (checkIfEnoughPoints()) windEstimateStep++;
      break;
    case 1:
      convertToDxDy();
      windEstimateStep++;
      break;
    case 2:
      if (updateEstimate()) windEstimateStep = 0;
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