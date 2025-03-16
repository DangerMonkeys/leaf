/*
 * baro.h
 *
 * Barometric Pressure Sensor MS5611-01BA03
 * SPI max 20MHz
 */

#pragma once

#include <Arduino.h>

#include "Leaf_SPI.h"
#include "LinearRegression.h"
#include "buttons.h"

// Sensor I2C address
#define ADDR_BARO 0x77

// Sensor commands
#define CMD_CONVERT_PRESSURE 0b01001000
#define CMD_CONVERT_TEMP 0b01011000

#define FILTER_VALS_MAX 20  // total array size max;

class Barometer {
 public:
  int32_t pressureFiltered;
  float altimeterSetting = 29.921;
  // cm raw pressure altitude calculated off standard altimeter setting (29.92)
  int32_t alt;
  // m raw pressure altitude (float)
  float altF;
  // the resulting altitude after being corrected by the altimeter setting
  int32_t altAdjusted;
  int32_t altAtLaunch;
  int32_t altAboveLaunch;
  int32_t altInitial;
  // instantaneous climbrate calculated with every pressure altitude measurement
  float climbRate;
  // filtered climb value to reduce noise
  int32_t climbRateFiltered;
  // long-term (several seconds) averaged climb rate for smoothing out glide ratio and other
  // calculations
  float climbRateAverage;
  // TODO: not yet used, but we may have a differently-averaged/filtered value for the grpahical
  // vario bar vs the text output for climbrate
  int32_t varioBar;

  // == Device Management ==
  // Initialize the baro sensor
  void init(void);
  void reset(void);

  // Reset launcAlt to current Alt (when starting a new log file, for example)
  void resetLaunchAlt(void);

  void update(bool startNewCycle, bool doTemp);

  void sleep(void);
  void wake(void);

  // == Device reading & data processing ==
  void adjustAltSetting(int8_t dir, uint8_t count);

  // == Test Functions ==
  void debugPrint(void);

 private:
  // temperature of air in the baro sensor (not to be confused with temperature reading from the
  // temp+humidity sensor)
  int32_t temp_;
  int32_t pressure_;
  int32_t pressureRegression_;

  // LinearRegression to average out noisy sensor readings
  LinearRegression<20> pressureLR_;

  // == User Settings for Vario ==

  // default samples to average (will be adjusted by VARIO_SENSE user setting)
  uint8_t filterValsPref_ = 3;

  int32_t pressureFilterVals_[FILTER_VALS_MAX + 1];  // use [0] as the index / bookmark
  float climbFilterVals_[FILTER_VALS_MAX + 1];       // use [0] as the index / bookmark

  // == Device Management ==

  // Track if we've put baro to sleep (in power off usb state)
  bool sleeping_ = false;

  uint32_t baroADCStartTime_ = 0;
  uint8_t processStep_ = 0;
  bool baroADCBusy_ = false;
  bool baroADCPressure_ = false;
  bool baroADCTemp_ = false;

  // == Device reading & data processing ==
  void calculatePressureAlt(void);
  void filterClimb(void);
  void filterPressure(void);  // TODO: Use or remove (currently unused)
  void calculateAlt(void);    // TODO: Use or remove (currently unused)

  // ======
  // Sensor Calibration Values (stored in chip PROM; must be read at startup before performing baro
  // calculations)
  uint16_t C_SENS_;
  uint16_t C_OFF_;
  uint16_t C_TCS_;
  uint16_t C_TCO_;
  uint16_t C_TREF_;
  uint16_t C_TEMPSENS_;

  // Digital read-out values
  uint32_t D1_P_;             //  digital pressure value (D1 in datasheet)
  uint32_t D1_Plast_ = 1000;  //  save previous value to use if we ever get a mis-read from the baro
                              //  sensor (initialize with a non zero starter value)
  uint32_t D2_T_;             //  digital temp value (D2 in datasheet)
  uint32_t D2_Tlast_ = 1000;  //  save previous value to use if we ever get a mis-read from the baro
                              //  sensor (initialize with a non zero starter value)
  int32_t lastAlt_ = 0;

  // Temperature Calculations
  int32_t dT_;

  // Compensation Values
  int64_t OFF1_;   // Offset at actual temperature
  int64_t SENS1_;  // Sensitivity at actual temperature

  // Extra compensation values for lower temperature ranges
  int32_t TEMP2_;
  int64_t OFF2_;
  int64_t SENS2_;

  // flag to set first climb rate sample to 0 (this allows us to wait for a second baro altitude
  // sample to calculate any altitude change)
  bool firstClimbInitialization_ = true;
};
extern Barometer baro;

// Conversion functions
int32_t baro_altToUnits(int32_t alt_input, bool units_feet);
float baro_climbToUnits(int32_t climbrate, bool units_fpm);
