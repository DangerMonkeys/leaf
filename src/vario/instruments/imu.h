#pragma once

#include <Arduino.h>
#include "hardware/motion_source.h"
#include "math/kalman.h"

#define POSITION_MEASURE_STANDARD_DEVIATION 0.1f
#define ACCELERATION_MEASURE_STANDARD_DEVIATION 0.3f

class IMU {
 public:
  IMU(IMotionSource* motionSource)
      : motionSource_(motionSource),
        kalmanvert_(pow(POSITION_MEASURE_STANDARD_DEVIATION, 2),
                    pow(ACCELERATION_MEASURE_STANDARD_DEVIATION, 2)) {}

  void init();
  void wake();
  void update();

  float getAccel();
  float getVelocity();

 private:
  bool processQuaternion();

  IMotionSource* motionSource_;

  // Kalman filter object for vertical climb rate and position
  KalmanFilterPA kalmanvert_;

  uint8_t startupCycleCount_;

  double accelVert_;
  double accelTot_;

  double zAvg_ = 1.0;  // Best guess for strength of gravity
  uint32_t tPrev_;     // Last time gravity guess was updated
};
extern IMU imu;
