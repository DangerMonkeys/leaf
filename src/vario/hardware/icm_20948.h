#pragma once

#include "dispatch/message_source.h"
#include "dispatch/pollable.h"

#include <ICM_20948.h>

class ICM20948 : public IPollable, IMessageSource {
 public:
  void init();

  // IPollable
  void update();

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

  uint32_t motionFifoPacketCount() const;
  uint32_t motionMatchedPacketCount() const;
  uint32_t motionMismatchedPacketCount() const;
  uint32_t motionPublishedSampleCount() const;
  uint32_t imuUpdateCallCount() const;
  uint32_t fifoNoDataCount() const;
  uint32_t invalidOrientationPacketCount() const;
  uint32_t invalidAccelerationPacketCount() const;
  uint32_t fifoResetCount() const;

  /// @brief Get the singleton ICM 20948 instance
  static ICM20948& getInstance() {
    static ICM20948 instance;
    return instance;
  }

 private:
  ICM_20948_I2C IMU_;
  etl::imessage_bus* bus_;
  uint32_t motionFifoPacketCount_ = 0;
  uint32_t motionMatchedPacketCount_ = 0;
  uint32_t motionMismatchedPacketCount_ = 0;
  uint32_t motionPublishedSampleCount_ = 0;
  uint32_t imuUpdateCallCount_ = 0;
  uint32_t fifoNoDataCount_ = 0;
  uint32_t invalidOrientationPacketCount_ = 0;
  uint32_t invalidAccelerationPacketCount_ = 0;
  uint32_t fifoResetCount_ = 0;
};
