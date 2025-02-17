/*
 * baro.h
 *
 * Barometric Pressure Sensor MS5611-01BA03
 * SPI max 20MHz
 */

#ifndef baro_h
#define baro_h

#include <Arduino.h>

#include "Leaf_SPI.h"
#include "buttons.h"

// Sensor I2C address
#define ADDR_BARO 0x77

// Sensor commands
#define CMD_CONVERT_PRESSURE 0b01001000
#define CMD_CONVERT_TEMP 0b01011000

// baro struct to hold most values
struct BARO {
  int32_t temp;  // temperature of air in the baro sensor (not to be confused with temperature
                 // reading from the temp+humidity sensor)
  int32_t pressure;
  int32_t pressureFiltered;
  int32_t pressureRegression;
  float altimeterSetting = 29.920;
  int32_t alt;          // raw pressure altitude calculated off standard altimeter setting (29.92)
  int32_t altAdjusted;  // the resulting altitude after being corrected by the altimeter setting
  int32_t altAtLaunch;
  int32_t altAboveLaunch;
  int32_t altInitial;
  int32_t climbRate;  // instantaneous climbrate calculated with every pressure altitude measurement
  int32_t climbRateFiltered;  // filtered climb value to reduce noise
  float climbRateAverage;     // long-term (several seconds) averaged climb rate for smoothing out
                              // glide ratio and other calculations
  int32_t varioBar;  // TODO: not yet used, but we may have a differently-averaged/filtered value
                     // for the grpahical vario bar vs the text output for climbrate
};
extern BARO baro;

// I2C Communication Functions
uint8_t baro_sendCommand(uint8_t command);
uint16_t baro_readCalibration(uint8_t PROMaddress);
uint32_t baro_readADC(void);

// Device Management
void baro_init(void);
void baro_reset(void);
void baro_resetLaunchAlt(void);
void baro_update(bool startNewCycle, bool doTemp);
void baro_sleep(void);
void baro_wake(void);

// Device reading & data processing
void baro_calculatePressure(void);
void baro_filterPressure(void);
void baro_calculateAlt(void);
void baro_updateClimb(void);
void baro_adjustAltSetting(int8_t dir, uint8_t count);

// Converstion functions
int32_t baro_altToUnits(int32_t alt_input, bool units_feet);
float baro_climbToUnits(int32_t climbrate, bool units_fpm);

// Test Functions
void baro_debugPrint(void);

#endif