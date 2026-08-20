// Sensor drivers that talk I2C, replaced for the emulator.
//
// The MS5611 barometer, ICM-20948 IMU and AHT20 temperature/humidity sensor are reached over I2C
// on hardware, and their readings arrive in the emulator as bus messages instead (played from a
// recording or injected over UDP).  So these classes keep their interfaces and their place in the
// boot sequence, but produce nothing themselves: every PressureUpdate, MotionUpdate and
// AmbientUpdate the firmware sees comes from the scenario.
//
// Everything downstream of these drivers -- baro filtering, the vario, the Kalman filter, wind
// estimation, logging -- is the real firmware code, unchanged.

#include <Arduino.h>

#include "hardware/aht20.h"
#include "hardware/icm_20948.h"
#include "hardware/ms5611.h"

// ---------------------------------------------------------------- MS5611 barometer

MS5611 ms5611;

void MS5611::init() { Serial.println("MS5611: emulated (pressure arrives as bus messages)"); }
void MS5611::update() {}
void MS5611::enableTemp(bool enable) { tempEnabled_ = enable; }
void MS5611::printCoeffs() { Serial.println("MS5611: no PROM coefficients in the emulator"); }
void MS5611::debugPrint() {}

PressureUpdate MS5611::getUpdate() { return PressureUpdate(millis(), 0); }
void MS5611::sendUpdate(const PressureUpdate& update) {
  if (bus_) bus_->receive(update);
}

// ---------------------------------------------------------------- ICM-20948 IMU

void ICM20948::init() { Serial.println("ICM20948: emulated (motion arrives as bus messages)"); }
void ICM20948::update() { imuUpdateCallCount_++; }

uint32_t ICM20948::motionFifoPacketCount() const { return motionFifoPacketCount_; }
uint32_t ICM20948::motionMatchedPacketCount() const { return motionMatchedPacketCount_; }
uint32_t ICM20948::motionMismatchedPacketCount() const { return motionMismatchedPacketCount_; }
uint32_t ICM20948::motionPublishedSampleCount() const { return motionPublishedSampleCount_; }
uint32_t ICM20948::imuUpdateCallCount() const { return imuUpdateCallCount_; }
uint32_t ICM20948::fifoNoDataCount() const { return fifoNoDataCount_; }
uint32_t ICM20948::invalidOrientationPacketCount() const { return invalidOrientationPacketCount_; }
uint32_t ICM20948::invalidAccelerationPacketCount() const {
  return invalidAccelerationPacketCount_;
}
uint32_t ICM20948::fifoResetCount() const { return fifoResetCount_; }

// ---------------------------------------------------------------- AHT20 temperature / humidity

AHT20 aht20;

void AHT20::update() {}
void AHT20::onUnexpectedState(const char* action, State actual) const {
  Serial.printf("AHT20: unexpected state during %s\n", action ? action : "?");
}
