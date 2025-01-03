#ifndef tempRH_h
#define tempRH_h

#include <Arduino.h>

#define ADDR_AHT20 0x38
#define TEMP_OFFSET \
  -3  // used for subtracting out any self-heating errors in the temp/humidity sensor

enum registers {
  sfe_aht20_reg_reset = 0xBA,
  sfe_aht20_reg_initialize = 0xBE,
  sfe_aht20_reg_measure = 0xAC,
};

struct {
  uint32_t humidity;
  uint32_t temperature;
} sensorData;

struct {
  uint8_t temperature : 1;
  uint8_t humidity : 1;
} sensorQueried;

void tempRH_update(uint8_t process_step);  // Repeating function called every ~second to trigger and
                                           // calculate measurements
bool tempRH_init(void);                    // Initialize the AHT20 device
bool tempRH_isConnected(void);             // Checks if the AHT20 is connected to the I2C bus
bool tempRH_available(void);               // Returns true if new data is available

// Measurement helper functions
uint8_t tempRH_getStatus(void);        // Returns the status byte
bool tempRH_isCalibrated(void);        // Returns true if the cal bit is set, false otherwise
bool tempRH_isBusy(void);              // Returns true if the busy bit is set, false otherwise
bool tempRH_initialize(void);          // Initialize for taking measurement
bool tempRH_triggerMeasurement(void);  // Trigger the AHT20 to take a measurement
void tempRH_readData(void);   // Read and parse the 6 bytes of data into raw humidity and temp
bool tempRH_softReset(void);  // Restart the sensor system without turning power off and on

float tempRH_getTemp(void);
float tempRH_getHumidity(void);

#endif