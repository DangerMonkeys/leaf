/*
 * Barometric Pressure Sensor MS5611-01BA03
 * SPI max 20MHz
 */

#pragma once

#include <stdint.h>

#include "pressure_source.h"

enum class MS5611State : uint8_t {
  None,
  Idle,
  MeasuringPressure,
  MeasuringTemperature,
};

class MS5611 : public IPressureSource {
 public:
  void init();
  PressureUpdateResult update();
  void startMeasurement();
  void enableTemp(bool enable);
  int32_t getPressure();

  void printCoeffs();
  void debugPrint();

 private:
  // Sensor Calibration Values (stored in chip PROM; must be read at startup before performing baro
  // calculations)
  uint16_t C_SENS_;
  uint16_t C_OFF_;
  uint16_t C_TCS_;
  uint16_t C_TCO_;
  uint16_t C_TREF_;
  uint16_t C_TEMPSENS_;

  // Digital read-out values

  // digital pressure value (D1 in datasheet)
  uint32_t D1_P_;
  // save previous value to use if we ever get a mis-read from the baro
  // sensor (initialize with a non zero starter value)
  uint32_t D1_Plast_ = 1000;
  // digital temp value (D2 in datasheet)
  uint32_t D2_T_;
  // save previous value to use if we ever get a mis-read from the baro
  // sensor (initialize with a non zero starter value)
  uint32_t D2_Tlast_ = 1000;

  // Temperature Calculations
  int32_t dT_;

  // Compensation Values
  int64_t OFF1_;   // Offset at actual temperature
  int64_t SENS1_;  // Sensitivity at actual temperature

  // Extra compensation values for lower temperature ranges
  int32_t TEMP2_;
  int64_t OFF2_;
  int64_t SENS2_;

  bool startMeasurement_ = false;
  MS5611State state_ = MS5611State::None;
  bool tempEnabled_ = true;
  uint32_t baroADCStartTime_ = 0;
};
