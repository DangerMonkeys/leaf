

#include <Arduino.h>
#include <limits>

#include "logging/log.h"
#include "wind_estimate.h"

WindEstimator windEstimator;

constexpr bool DEBUG_WIND_ESTIMATE = false;  // enable for verbose serial printing

struct WindEstimateAdjustment {
  // Change in easterly component of windspeed, m/s
  float dwx;

  // Change in northerly component of windspeed, m/s
  float dwy;

  // Change in aircraft airspeed, m/s
  float dairspeed;
};

constexpr int ADJUSTMENT_COUNT = 6;
constexpr WindEstimateAdjustment adjustments[ADJUSTMENT_COUNT] = {
    WindEstimateAdjustment{.dwx = 0.1f, .dwy = 0, .dairspeed = 0},
    WindEstimateAdjustment{.dwx = -0.1f, .dwy = 0, .dairspeed = 0},
    WindEstimateAdjustment{.dwx = 0, .dwy = 0.1f, .dairspeed = 0},
    WindEstimateAdjustment{.dwx = 0, .dwy = -0.1f, .dairspeed = 0},
    WindEstimateAdjustment{.dwx = 0, .dwy = 0, .dairspeed = 0.1f},
    WindEstimateAdjustment{.dwx = 0, .dwy = 0, .dairspeed = -0.1f},
};

void WindEstimator::on_receive(const GpsReading& msg) {
  if (msg.gps.course.isUpdated() || msg.gps.speed.isUpdated()) {
    GroundVelocity v = {.trackAngle = (float)(DEG_TO_RAD * gps.course.deg()),
                        .speed = (float)gps.speed.mps()};

    if (flightTimer_isRunning()) submitVelocityForWindEstimate(v);
  }
}

void WindEstimator::averageSamplePoints() {
  for (int i = 0; i < BIN_COUNT; i++) {
    float sumAngle = 0;
    float sumSpeed = 0;
    for (int j = 0; j < totalSamples_.bin[i].sampleCount; j++) {
      sumAngle += totalSamples_.bin[i].angle[j];
      sumSpeed += totalSamples_.bin[i].speed[j];
    }
    totalSamples_.bin[i].averageAngle = sumAngle / totalSamples_.bin[i].sampleCount;
    totalSamples_.bin[i].averageSpeed = sumSpeed / totalSamples_.bin[i].sampleCount;
  }
}

inline float dxOf(float angle, float speed) { return cos(angle) * speed; }

inline float dyOf(float angle, float speed) { return sin(angle) * speed; }

constexpr bool ONLY_CONVERT_AVERAGE = false;
void WindEstimator::convertToDxDy() {
  for (int b = 0; b < BIN_COUNT; b++) {
    // convert average angle/speed into average dx/dy
    totalSamples_.bin[b].averageDx =
        dxOf(totalSamples_.bin[b].averageAngle, totalSamples_.bin[b].averageSpeed);
    totalSamples_.bin[b].averageDy =
        dyOf(totalSamples_.bin[b].averageAngle, totalSamples_.bin[b].averageSpeed);

    // convert all angle/speed into all dx/dy
    if (!ONLY_CONVERT_AVERAGE) {
      for (int s = 0; s < totalSamples_.bin[b].sampleCount; s++) {
        totalSamples_.bin[b].dx[s] =
            dxOf(totalSamples_.bin[b].angle[s], totalSamples_.bin[b].speed[s]);
        totalSamples_.bin[b].dy[s] =
            dyOf(totalSamples_.bin[b].angle[s], totalSamples_.bin[b].speed[s]);
      }
    }
  }
}

bool WindEstimator::checkIfEnoughPoints() {
  bool enough = false;  // assume we don't have enough

  uint8_t populatedBinCount = 0;
  constexpr uint8_t POPULATED_BINS_REQUIRED = 3;
  uint8_t continuousEmptyBinCount = 0;
  constexpr uint8_t BINS_REQUIRED_FOR_SEMI_CIRCLE = BIN_COUNT / 2;

  uint8_t firstBinToHavePoints = 0;
  bool haveAStartingBin = false;
  uint8_t populatedSpan = 0;

  for (int i = 0; i < BIN_COUNT; i++) {
    if (totalSamples_.bin[i].sampleCount > 0) {
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
      if (continuousEmptyBinCount >= BINS_REQUIRED_FOR_SEMI_CIRCLE) {
        return false;
      }
    }
  }

  // if we have enough bins and they span more than a semi circle, we have enough
  if (populatedBinCount >= POPULATED_BINS_REQUIRED &&
      populatedSpan >= BINS_REQUIRED_FOR_SEMI_CIRCLE) {
    enough = true;
  }

  return enough;
}

float WindEstimator::errorOf(float wx, float wy, float airspeed) const {
  float sumSquareError = 0;
  int n = 0;
  for (int bin = 0; bin < BIN_COUNT; bin++) {
    for (int sample = 0; sample < totalSamples_.bin[bin].sampleCount; sample++) {
      float dx = totalSamples_.bin[bin].dx[sample] - wx;
      float dy = totalSamples_.bin[bin].dy[sample] - wy;
      float dr = sqrt(dx * dx + dy * dy) - airspeed;
      sumSquareError += dr * dr;
      n++;
    }
  }
  return sqrt(sumSquareError / n);
}

inline float speedOf(float wx, float wy) { return sqrt(wx * wx + wy * wy); }

inline float directionOf(float wx, float wy) {
  // atan2 takes y, x in cpp/arduino, unlike other languages and tools which take x, y
  return atan2(wy, wx);
}

bool WindEstimator::updateEstimate() {
  updateCount_++;
  float wx = dxOf(windEstimate_.windDirectionTrue, windEstimate_.windSpeed);
  float wy = dyOf(windEstimate_.windDirectionTrue, windEstimate_.windSpeed);
  float bestError = errorOf(wx, wy, windEstimate_.airspeed);

  if (DEBUG_WIND_ESTIMATE) {
    Serial.print("Begin Estimate!  Wx: ");
    Serial.print(wx);
    Serial.print(" wy: ");
    Serial.print(wy);
    Serial.print(" Dir: ");
    Serial.print(windEstimate_.windDirectionTrue);
    Serial.print(" Spd: ");
    Serial.print(windEstimate_.windSpeed);
    Serial.print(" Airspd: ");
    Serial.print(windEstimate_.airspeed);
    Serial.print(" Err: ");
    Serial.println(bestError);
  }

  int bestAdjustment = -1;

  // TODO: split each adjustment into a separate invocation of updateEstimate if needed for
  // responsitivity (or remove this TODO if already responsive enough)
  for (int a = 0; a < ADJUSTMENT_COUNT; a++) {
    float newError = errorOf(wx + adjustments[a].dwx, wy + adjustments[a].dwy,
                             windEstimate_.airspeed + adjustments[a].dairspeed);

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
    betterCount_++;
    // New estimate is available
    windEstimate_.airspeed += adjustments[bestAdjustment].dairspeed;
    wx += adjustments[bestAdjustment].dwx;
    wy += adjustments[bestAdjustment].dwy;
    windEstimate_.windSpeed = speedOf(wx, wy);
    windEstimate_.windDirectionTrue = directionOf(wx, wy);
    windEstimate_.windDirectionFrom = windEstimate_.windDirectionTrue + PI;  // add 180 degrees
    windEstimate_.error = bestError;

    if (DEBUG_WIND_ESTIMATE) {
      Serial.print("UPDATE ESTIMATE! Wx: ");
      Serial.print(wx);
      Serial.print(" wy: ");
      Serial.print(wy);
      Serial.print(" Dir: ");
      Serial.print(windEstimate_.windDirectionTrue);
      Serial.print(" Spd: ");
      Serial.print(windEstimate_.windSpeed);
      Serial.print(" Airspd: ");
      Serial.print(windEstimate_.airspeed);
      Serial.print(" Err: ");
      Serial.println(windEstimate_.error);
    }

    return true;
  } else {
    // Current estimate cannot be improved upon
  }
  windEstimate_.validEstimate = true;

  return true;
}

void WindEstimator::estimateWind() {
  int estimateTimeStamp = micros();
  bool enoughPoints;
  switch (windEstimateStep_) {
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
        windEstimateStep_++;
      }
      break;
    case 1:
      if (updateEstimate()) windEstimateStep_ = 0;
      if (DEBUG_WIND_ESTIMATE) {
        Serial.print("**TIME** update estimate: ");
        Serial.println(micros() - estimateTimeStamp);
      }
      break;
  }
}

void WindEstimator::clearWindEstimate() {
  // clear the estimate
  windEstimate_.validEstimate = false;
  windEstimate_.windSpeed = 0;
  windEstimate_.windDirectionTrue = 0;
  windEstimate_.airspeed = STANDARD_AIRSPEED;
  windEstimate_.error = std::numeric_limits<float>::max();
  windEstimate_.recentBin = -1;

  // clear the sample points
  // (we don't need to actually erase them; just set indices and count to 0)
  for (int b = 0; b < BIN_COUNT; b++) {
    totalSamples_.bin[b].index = 0;
    totalSamples_.bin[b].sampleCount = 0;
  }
}

void WindEstimator::submitVelocityForWindEstimate(GroundVelocity groundVelocity) {
  // GroundVelocity track relative to current wind estimate (as the wind center moves, we
  //   want to move the bins, too, so that we can good sampling all around the circle)
  float relativeAngle = groundVelocity.trackAngle;  // start with existing track angle

  // if we have a valid Wind Estimate, calculate the sample point relative to wind-center
  // ..actually though we'll only use 1/2 of the wind speed, so the estimate can't really
  //   explode and prevent future sample points from registering properly
  if (windEstimate_.validEstimate) {
    float dx = dxOf(groundVelocity.trackAngle, groundVelocity.speed);
    float dy = dyOf(groundVelocity.trackAngle, groundVelocity.speed);
    float wx = dxOf(windEstimate_.windDirectionTrue, windEstimate_.windSpeed / 2);
    float wy = dyOf(windEstimate_.windDirectionTrue, windEstimate_.windSpeed / 2);
    relativeAngle = directionOf(dx - wx, dy - wy);
  }

  // stay positive, since we'll search bins from 0 to 2*PI
  // (if wind estimate is not yet valid, relativeAngle will just be the original ground track angle)
  if (relativeAngle < 0) {
    relativeAngle += 2 * PI;
  }

  // now sort into appropriate bin
  constexpr float BIN_ANGLE_SPAN = 2 * PI / BIN_COUNT;
  for (int b = 0; b < BIN_COUNT; b++) {
    if (relativeAngle < (b + 1) * BIN_ANGLE_SPAN) {
      totalSamples_.bin[b].angle[totalSamples_.bin[b].index] = groundVelocity.trackAngle;
      totalSamples_.bin[b].speed[totalSamples_.bin[b].index] = groundVelocity.speed;
      totalSamples_.bin[b].index++;
      totalSamples_.bin[b].sampleCount++;

      windEstimate_.recentBin = b;

      // track index and count
      if (totalSamples_.bin[b].sampleCount > SAMPLES_PER_BIN) {
        totalSamples_.bin[b].sampleCount = SAMPLES_PER_BIN;
      }
      if (totalSamples_.bin[b].index >= SAMPLES_PER_BIN) {
        totalSamples_.bin[b].index = 0;
      }

      break;
    }
  }

  // use this latest GPS velocity to calculate approximate realtime airspeed
  // ...if we have a valid wind estimate
  if (windEstimate_.validEstimate) {
    windEstimate_.airspeedLive =
        speedOf(dxOf(groundVelocity.trackAngle, groundVelocity.speed) +
                    dxOf(windEstimate_.windDirectionFrom, windEstimate_.windSpeed),
                dyOf(groundVelocity.trackAngle, groundVelocity.speed) +
                    dyOf(windEstimate_.windDirectionFrom, windEstimate_.windSpeed));
  }
}
