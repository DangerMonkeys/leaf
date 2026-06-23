#pragma once

#include <Arduino.h>

#include "dispatch/message_sink.h"
#include "dispatch/message_source.h"
#include "dispatch/message_types.h"
#include "math/kalman.h"

#define POSITION_MEASURE_STANDARD_DEVIATION 0.1f
#define ACCELERATION_MEASURE_STANDARD_DEVIATION 0.3f

class IMU : public MessageSink<IMU, MotionUpdate>, public IMessageSource {
 public:
  IMU();

  void wake();

  // MessageSink<IMU, MotionUpdate>
  void on_receive(const MotionUpdate& msg);
  void on_receive_unknown(const etl::imessage& msg) {}

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

  bool accelValid();
  float getAccel();

  bool velocityValid();
  float getVelocity();

  uint16_t gravityInitSamplesRemaining() const;
  float gravityEstimate() const;
  float lastWorldVerticalAccel() const;
  bool worldVerticalAccelValid() const;
  uint16_t gravityInitResetCount() const;
  float lastRejectedGravityEstimate() const;
  uint32_t motionSampleCount() const;
  uint32_t motionSampleBaroNotReadyCount() const;
  uint32_t motionSampleMissingFieldsCount() const;
  uint32_t motionSampleProcessedCount() const;
  uint32_t motionSampleRejectedQuaternionCount() const;
  uint32_t gravityInitSampleCount() const;
  uint32_t gravityUpdateCandidateCount() const;
  uint32_t gravityUpdateAcceptedCount() const;
  uint32_t gravityUpdateRejectedAccelCount() const;
  uint32_t gravityUpdateRejectedVerticalCount() const;
  uint32_t gravityUpdateRejectedTimeCount() const;
  uint32_t gravityUpdateRejectedPlausibilityCount() const;
  uint32_t gravityUpdateSlewLimitedCount() const;
  uint32_t kalmanUpdateSampleCount() const;

 private:
  void processMotion(const MotionUpdate& m);

  etl::imessage_bus* bus_ = nullptr;

  // Kalman filter object for vertical climb rate and position
  KalmanFilterPA kalmanvert_;

  bool kalmanInitialized_ = false;

  double accelVert_;
  bool validAccelVert_ = false;

  double accelTot_;
  bool validAccelTot_ = false;

  // Best estimate for strength of gravity
  double gravity_ = 1.0;

  double lastWorldVerticalAccel_ = 0.0;
  bool validLastWorldVerticalAccel_ = false;
  double lastRejectedGravity_ = 0.0;
  uint16_t gravityInitResetCount_ = 0;

  // Last time gravity estimate was updated
  uint32_t tLastGravityUpdate_ = 0;

  uint32_t motionSampleCount_ = 0;
  uint32_t motionSampleBaroNotReadyCount_ = 0;
  uint32_t motionSampleMissingFieldsCount_ = 0;
  uint32_t motionSampleProcessedCount_ = 0;
  uint32_t motionSampleRejectedQuaternionCount_ = 0;
  uint32_t gravityInitSampleCount_ = 0;
  uint32_t gravityUpdateCandidateCount_ = 0;
  uint32_t gravityUpdateAcceptedCount_ = 0;
  uint32_t gravityUpdateRejectedAccelCount_ = 0;
  uint32_t gravityUpdateRejectedVerticalCount_ = 0;
  uint32_t gravityUpdateRejectedTimeCount_ = 0;
  uint32_t gravityUpdateRejectedPlausibilityCount_ = 0;
  uint32_t gravityUpdateSlewLimitedCount_ = 0;
  uint32_t kalmanUpdateSampleCount_ = 0;
  uint16_t gravityVerticalRejectCount_ = 0;
};
extern IMU imu;
