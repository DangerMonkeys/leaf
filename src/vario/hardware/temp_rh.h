#ifndef tempRH_h
#define tempRH_h

#include <Arduino.h>

struct SensorData {
  uint32_t humidity;
  uint32_t temperature;
};

struct SensorStatus {
  bool temperature = true;
  bool humidity = true;
};

class AHT20 {
 public:
  // Initialize the AHT20 device
  bool init(void);

  // Repeating function called every ~second to trigger
  // and calculate measurements
  void update(uint8_t process_step);

  float getTemp() { return ambientTemp_; }

  float getHumidity() { return ambientHumidity_; }

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

  // calculated deg C
  float ambientTemp_ = 0;

  // calculated % relative humidity
  float ambientHumidity_ = 0;

  uint8_t currentlyProcessing_ = false;

  SensorData sensorData_;

  SensorStatus sensorQueried_;
};
extern AHT20 tempRH;

#endif
