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
  float verticalAccel() const;
  float kalmanAccelInput() const;
  bool kalmanValid() const;
  float kalmanPosition() const;
  float kalmanVelocity() const;
  float kalmanAcceleration() const;
  unsigned long lastMotionTime() const;
  float lastDeviceAccelX() const;
  float lastDeviceAccelY() const;
  float lastDeviceAccelZ() const;
  float lastQuatX() const;
  float lastQuatY() const;
  float lastQuatZ() const;
  float lastWorldAccelX() const;
  float lastWorldAccelY() const;
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
  uint16_t startupSamplesCompleted() const;
  uint16_t startupSamplesRequired() const;

 private:
  void processMotion(const MotionUpdate& m);
  bool startupReadinessTimedOut() const;

  etl::imessage_bus* bus_ = nullptr;

  // Kalman filter object for vertical climb rate and position
  KalmanFilterPA kalmanvert_;

  uint16_t kalmanStartupSamplesRemaining_;

  double accelVert_;
  double kalmanAccelVert_ = 0.0;
  bool validAccelVert_ = false;

  double accelTot_;
  bool validAccelTot_ = false;

  // Best estimate for strength of gravity
  double gravity_ = 1.0;

  double lastWorldVerticalAccel_ = 0.0;
  double lastWorldAccelX_ = 0.0;
  double lastWorldAccelY_ = 0.0;
  double lastDeviceAccelX_ = 0.0;
  double lastDeviceAccelY_ = 0.0;
  double lastDeviceAccelZ_ = 0.0;
  double lastQuatX_ = 0.0;
  double lastQuatY_ = 0.0;
  double lastQuatZ_ = 0.0;
  unsigned long lastMotionTime_ = 0;
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
  uint16_t startupNonVertSamples_ = 0;
  uint32_t startupReadinessStartMs_ = 0;
};
extern IMU imu;
