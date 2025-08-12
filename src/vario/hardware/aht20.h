#pragma once

#include <Arduino.h>

#include "dispatch/message_source.h"
#include "dispatch/pollable.h"

struct SensorData {
  uint32_t humidity;
  uint32_t temperature;
};

struct SensorStatus {
  bool temperature = true;
  bool humidity = true;
};

class AHT20 : public IPollable, IMessageSource {
 public:
  void init();

  // IPollable
  void update();

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

  /// @brief Get the singleton AHT20 instance
  static AHT20& getInstance() {
    static AHT20 instance;
    return instance;
  }

 private:
  // Checks if the AHT20 is connected to the I2C bus
  bool isConnected(void);

  // Returns true if new data is available
  bool available(void);

  // Returns the status byte
  uint8_t getStatus(void);

  // Returns true if the cal bit is set, false otherwise
  bool isCalibrated(void);

  // Returns true if the busy bit is set, false otherwise
  bool isBusy(void);

  // Initialize for taking measurement
  bool initialize(void);

  // Trigger the AHT20 to take a measurement
  bool triggerMeasurement(void);

  // Read and parse the 6 bytes of data into raw humidity and temp
  void readData(void);

  // Restart the sensor system without turning power off and on
  bool softReset(void);

  bool measurementStarted_ = false;

  uint8_t currentlyMeasuring_ = false;

  SensorData sensorData_;

  SensorStatus sensorQueried_;

  unsigned long measurementInitiated_;

  etl::imessage_bus* bus_ = nullptr;
};
