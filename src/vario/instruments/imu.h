#pragma once

#include <Arduino.h>
#include "etl/message_bus.h"

#include "dispatch/message_source.h"
#include "dispatch/message_types.h"
#include "math/kalman.h"

#define POSITION_MEASURE_STANDARD_DEVIATION 0.1f
#define ACCELERATION_MEASURE_STANDARD_DEVIATION 0.3f

class IMU : public etl::message_router<IMU, MotionUpdate>, public IMessageSource {
 public:
  IMU()
      : kalmanvert_(pow(POSITION_MEASURE_STANDARD_DEVIATION, 2),
                    pow(ACCELERATION_MEASURE_STANDARD_DEVIATION, 2)) {}

  void init();
  void wake();

  void subscribe(etl::imessage_bus* bus) { bus->subscribe(*this); }

  // etl::message_router<IMU, MotionUpdate>
  void on_receive(const MotionUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

  float getAccel();
  float getVelocity();

 private:
  void processQuaternion(const MotionUpdate& m);

  etl::imessage_bus* bus_ = nullptr;

  // Kalman filter object for vertical climb rate and position
  KalmanFilterPA kalmanvert_;

  bool kalmanInitialized_ = false;

  uint8_t startupCycleCount_;

  double accelVert_;
  double accelTot_;

  double zAvg_ = 1.0;  // Best guess for strength of gravity
  uint32_t tPrev_;     // Last time gravity guess was updated
};
extern IMU imu;
