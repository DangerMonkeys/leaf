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
  void attach(etl::imessage_bus* bus) { bus_ = bus; }

  /// @brief Get the singleton ICM 20948 instance
  static ICM20948& getInstance() {
    static ICM20948 instance;
    return instance;
  }

 private:
  ICM_20948_I2C IMU_;
  etl::imessage_bus* bus_;
};
