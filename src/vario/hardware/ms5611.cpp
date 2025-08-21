#include "hardware/ms5611.h"

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

#include "diagnostics/fatal_error.h"
#include "dispatch/message_types.h"

MS5611 ms5611;

// Sensor I2C address
#define ADDR_BARO 0x77

// Sensor commands
#define CMD_CONVERT_PRESSURE 0b01001000
#define CMD_CONVERT_TEMP 0b01011000

// Minimum time between starting consecutive measurements
const unsigned long MEASUREMENT_PERIOD_MICROS = 9000;

// vvv I2C Communication Functions vvv

uint8_t ms5611_sendCommand(uint8_t command) {
  Wire.beginTransmission(ADDR_BARO);
  Wire.write(command);
  uint8_t result = Wire.endTransmission();
  return result;
}

uint16_t ms5611_readCalibration(uint8_t PROMaddress) {
  uint16_t value = 0;  // This will be the final 16-bit output from the ADC
  uint8_t command =
      0b10100000;  // The command to read from the specified address is 1 0 1 0 ad2 ad1 ad0 0
  command += (PROMaddress << 1);  // Add PROM address bits to the command byte

  ms5611_sendCommand(command);
  Wire.requestFrom(ADDR_BARO, 2);
  value += (Wire.read() << 8);
  value += (Wire.read());

  return value;
}

uint32_t ms5611_readADC() {
  uint32_t value = 0;  // This will be the final 24-bit output from the ADC
  ms5611_sendCommand(0b00000000);
  Wire.requestFrom(ADDR_BARO, 3);
  value += (Wire.read() << 16);
  value += (Wire.read() << 8);
  value += (Wire.read());

  return value;
}

// ^^^ I2C Communication Functions ^^^

void ms5611_reset(void) {
  // This is the command to reset, and for the sensor to copy
  // calibration data into the register as needed
  unsigned char command = 0b00011110;
  ms5611_sendCommand(command);
  delay(3);  // delay time required before sensor is ready
}

void MS5611::init() {
  // reset sensor for initialization
  ms5611_reset();
  delay(2);

  // read calibration values
  C_SENS_ = ms5611_readCalibration(1);
  C_OFF_ = ms5611_readCalibration(2);
  C_TCS_ = ms5611_readCalibration(3);
  C_TCO_ = ms5611_readCalibration(4);
  C_TREF_ = ms5611_readCalibration(5);
  C_TEMPSENS_ = ms5611_readCalibration(6);

  state_ = MS5611State::Idle;

  // Start first measurement
  update();
}

void MS5611::enableTemp(bool enable) { tempEnabled_ = enable; }

void MS5611::update() {
  unsigned long microsNow;

  switch (state_) {
    case MS5611State::Idle:  // SEND CONVERT PRESSURE COMMAND
      // Prep baro sensor ADC to read raw pressure value
      // (then come back for step 2 later)
      ms5611_sendCommand(CMD_CONVERT_PRESSURE);
      baroADCStartTime_ = micros();
      state_ = MS5611State::MeasuringPressure;
      return;

    case MS5611State::MeasuringPressure:  // READ PRESSURE THEN MAYBE SEND CONVERT TEMP COMMAND
      microsNow = micros();
      if (microsNow - baroADCStartTime_ <= MEASUREMENT_PERIOD_MICROS) {
        return;  // Sensor is busy measuring pressure
      }

      D1_P_ = ms5611_readADC();  // Read raw pressure value
      // prep and read
      if (D1_P_ == 0)
        D1_P_ = D1_Plast_;  // use the last value if we get an invalid read
      else
        D1_Plast_ = D1_P_;  // otherwise save this value for next time if needed
      // baroTimeStampTemp = micros();

      if (tempEnabled_) {
        ms5611_sendCommand(CMD_CONVERT_TEMP);  // Prep baro sensor ADC to read raw temperature value
                                               // (then come back for step 3 in ~10ms)
        baroADCStartTime_ = micros();
        state_ = MS5611State::MeasuringTemperature;
        return;
      } else {
        etl::imessage_bus* bus = bus_;
        if (bus) {
          bus->receive(getUpdate());
        }
        state_ = MS5611State::Idle;
        return;
      }

    case MS5611State::MeasuringTemperature:  // READ TEMP
      microsNow = micros();
      if (microsNow - baroADCStartTime_ <= MEASUREMENT_PERIOD_MICROS) {
        return;  // Sensor is busy measuring temperature
      }

      D2_T_ = ms5611_readADC();  // read digital temp data
      // read
      if (D2_T_ == 0)
        D2_T_ = D2_Tlast_;  // use the last value if we get a misread
      else
        D2_Tlast_ = D2_T_;  // otherwise save this value for next time if needed
      etl::imessage_bus* bus = bus_;
      if (bus) {
        bus->receive(getUpdate());
      }
      state_ = MS5611State::Idle;
      return;
  }

  fatalError("MS5611 was in unknown/unhandled state %d\n", (int)state_);
}

PressureUpdate MS5611::getUpdate() {
  unsigned long t = millis();

  // TODO: replace division by powers of 2 with >> and multiplication by powers of 2 with <<

  // calculate temperature (in 100ths of degrees C, from -4000 to 8500)
  dT_ = D2_T_ - ((int32_t)C_TREF_) * 256;
  int32_t TEMP = 2000 + (((int64_t)dT_) * ((int64_t)C_TEMPSENS_)) / pow(2, 23);

  // calculate sensor offsets to use in pressure & altitude calcs
  OFF1_ = (int64_t)C_OFF_ * pow(2, 16) + (((int64_t)C_TCO_) * dT_) / pow(2, 7);
  SENS1_ = (int64_t)C_SENS_ * pow(2, 15) + ((int64_t)C_TCS_ * dT_) / pow(2, 8);

  // low temperature compensation adjustments
  TEMP2_ = 0;
  OFF2_ = 0;
  SENS2_ = 0;
  if (TEMP < 2000) {
    TEMP2_ = pow((int64_t)dT_, 2) / pow(2, 31);
    OFF2_ = 5 * pow((TEMP - 2000), 2) / 2;
    SENS2_ = 5 * pow((TEMP - 2000), 2) / 4;
  }
  // very low temperature compensation adjustments
  if (TEMP < -1500) {
    OFF2_ = OFF2_ + 7 * pow((TEMP + 1500), 2);
    SENS2_ = SENS2_ + 11 * pow((TEMP + 1500), 2) / 2;
  }
  TEMP = TEMP - TEMP2_;
  OFF1_ = OFF1_ - OFF2_;
  SENS1_ = SENS1_ - SENS2_;

  // temperature of air in the baro sensor (not to be confused with temperature reading from the
  // temp+humidity sensor)
  int32_t temp = TEMP;

  // calculate temperature compensated pressure (in 100ths of mbars)
  int32_t pressure = ((uint64_t)D1_P_ * SENS1_ / (int64_t)pow(2, 21) - OFF1_) / pow(2, 15);

  return PressureUpdate(t, pressure);
}

void MS5611::printCoeffs() {
  Serial.println("Baro initialization values:");
  Serial.print("  C_SENS:");
  Serial.println(C_SENS_);
  Serial.print("  C_OFF:");
  Serial.println(C_OFF_);
  Serial.print("  C_TCS:");
  Serial.println(C_TCS_);
  Serial.print("  C_TCO:");
  Serial.println(C_TCO_);
  Serial.print("  C_TREF:");
  Serial.println(C_TREF_);
  Serial.print("  C_TEMPSENS:");
  Serial.println(C_TEMPSENS_);
  Serial.print("  D1:");
  Serial.println(D1_P_);
  Serial.print("  D2:");
  Serial.println(D2_T_);
  Serial.print("  dT:");
  Serial.println(dT_);
  Serial.print("  OFF1:");
  Serial.println(OFF1_);
  Serial.print("  SENS1:");
  Serial.println(SENS1_);

  Serial.println(" ");
}

void MS5611::debugPrint() {
  Serial.print("D1_P:");
  Serial.print(D1_P_);
  Serial.print(", D2_T:");
  Serial.print(D2_T_);  // has been zero, perhaps because GPS serial buffer processing delayed the
                        // ADC prep for reading this from baro chip
}