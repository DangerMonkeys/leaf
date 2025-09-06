#pragma once

#include <Arduino.h>

#include "dispatch/message_source.h"
#include "dispatch/pollable.h"
#include "utils/state_assert_mixin.h"

struct SensorData {
  uint32_t humidity;
  uint32_t temperature;
};

class AHT20 : public IPollable, IMessageSource, private StateAssertMixin<AHT20> {
 public:
  // IPollable
  void update();

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

 private:
  enum class State : uint8_t {
    Uninitialized,
    WaitingForPowerOn,
    WaitingForCalMeasurement,
    WaitingForInitialMeasurement,
    Measuring,
    Idle,
  };

  State state() const { return state_; }
  void onUnexpectedState(const char* action, State actual) const;
  friend struct StateAssertMixin<AHT20>;

  void beginInit();
  void waitForPowerOn();
  void waitForCalMeasurement();
  void startFirstMeasurement();
  void maybeTriggerMeasurement();
  void completeMeasurement();

  // Checks if the AHT20 is connected to the I2C bus
  bool isConnected();

  // Returns the status byte
  uint8_t getStatus();

  // Returns true if the cal bit is set, false otherwise
  bool isCalibrated();

  // Returns true if the busy bit is set, false otherwise
  bool isBusy();

  // Initialize for taking measurement
  bool initialize();

  // Trigger the AHT20 to take a measurement
  void triggerMeasurement(State onSuccess, State onFailure);

  // Read and parse the 6 bytes of data into raw humidity and temp
  bool readData(SensorData& sensorData);

  // Restart the sensor system without turning power off and on
  bool softReset();

  State state_ = State::Uninitialized;

  unsigned long tLastAction_;
  unsigned long dtMeasurement_;

  etl::imessage_bus* bus_ = nullptr;
};

extern AHT20 aht20;
