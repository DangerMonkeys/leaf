#pragma once

#include <Arduino.h>
#include "etl/message_bus.h"
#include "math/kalman.h"

#include "dispatch/message_types.h"

#define POSITION_MEASURE_STANDARD_DEVIATION 0.1f
#define ACCELERATION_MEASURE_STANDARD_DEVIATION 0.3f

class IMU : public etl::message_router<IMU, MotionUpdate> {
 public:
  IMU();

  void init();
  void wake();

  void subscribe(etl::imessage_bus* bus) { bus->subscribe(*this); }

  // etl::message_router<IMU, MotionUpdate>
  void on_receive(const MotionUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

  bool accelValid();
  float getAccel();

  bool velocityValid();
  float getVelocity();

 private:
  void processQuaternion(const MotionUpdate& m);

  // Kalman filter object for vertical climb rate and position
  KalmanFilterPA kalmanvert_;

  bool kalmanInitialized_ = false;

  double accelVert_;
  bool validAccelVert_ = false;

  double accelTot_;
  bool validAccelTot_ = false;

  // Best estimate for strength of gravity
  double gravity_ = 1.0;

  // Number of samples remaining to collect to initialize estimate of gravity
  uint16_t gravityInitCount_;

  // Last time gravity estimate was updated
  uint32_t tLastGravityUpdate_;
};
extern IMU imu;
