#include "hardware/temp_rh.h"

#include "hardware/Leaf_I2C.h"

AHT20 tempRH;

#define DEBUG_TEMPRH 0  // flag for outputting debugf messages on UBS serial port

#define ADDR_AHT20 0x38
// used for subtracting out any self-heating errors in the temp/humidity sensor
#define TEMP_OFFSET -3

enum registers {
  sfe_aht20_reg_reset = 0xBA,
  sfe_aht20_reg_initialize = 0xBE,
  sfe_aht20_reg_measure = 0xAC,
};

bool AHT20::init() {
  bool success = true;

  if (!isConnected()) {
    success = false;

    if (DEBUG_TEMPRH) Serial.println("Temp_RH - AHT20 Temp Humidity sensor NOT FOUND!");

  } else {
    if (DEBUG_TEMPRH) Serial.println("Temp_RH - AHT20 Temp Humidity sensor FOUND!");

    // Wait 40 ms after power-on before reading temp or humidity. Datasheet pg 8
    delay(40);

    // Check if the calibrated bit is set. If not, init the sensor.
    if (!isCalibrated()) {
      // Send 0xBE0800
      initialize();

      // Immediately trigger a measurement. Send 0xAC3300
      triggerMeasurement();

      delay(75);  // Wait for measurement to complete

      uint8_t counter = 0;
      while (isBusy()) {
        delay(1);
        if (counter++ > 100) success = false;  // Give up after 100ms
      }

      // This calibration sequence is not completely proven. It's not clear how and when the cal bit
      // clears This seems to work but it's not easily testable
      if (!isCalibrated()) success = false;
    }

    // Check that the cal bit has been set
    if (!isCalibrated()) success = false;

    // Mark all datums as fresh (not read before)
    sensorQueried_.temperature = true;
    sensorQueried_.humidity = true;

    // Get a fresh initial measurement
    update(1);
    delay(100);
    update(2);
  }

  if (DEBUG_TEMPRH) {
    if (success)
      Serial.println("Temp_RH - SUCCESS: AHT20 Temp Humidity sensor calibrated!");
    else
      Serial.println("Temp_RH - FAIL: AHT20 Temp Humidity sensor not initialized/calibrated");
  }

  return success;
}

// don't call more often than every 1-2 seconds, or sensor will heat up slightly above ambient
void AHT20::update(uint8_t process_step) {
  // measurements must first be triggered, then >75ms later can be read
  if (process_step == 1) {
    if (!currentlyProcessing_)
      triggerMeasurement();  // if we haven't yet processed a prior measurement, don't
                             // trigger a new one
    currentlyProcessing_ = true;
  } else if (process_step == 2) {
    if (!isBusy()) {  // if busy, skip this and we'll try again next time
      readData();
      ambientTemp_ = ((float)sensorData_.temperature / 1048576) * 200 - 50;
      ambientTemp_ += TEMP_OFFSET;
      ambientHumidity_ = ((float)sensorData_.humidity / 1048576) * 100;
      currentlyProcessing_ = false;
      if (DEBUG_TEMPRH) {
        Serial.print("Temp_RH - Temp: ");
        Serial.print(ambientTemp_);
        Serial.print("  Humidity: ");
        Serial.println(ambientHumidity_);
      }
    } else {
      if (DEBUG_TEMPRH) Serial.println("Temp_RH - missed values due to sensor busy");
    }
  }
}

bool AHT20::softReset() {
  Wire.beginTransmission(ADDR_AHT20);
  Wire.write(sfe_aht20_reg_reset);
  if (Wire.endTransmission() == 0) return true;
  return false;
}

bool AHT20::available() {
  if (!measurementStarted_) {
    triggerMeasurement();
    measurementStarted_ = true;
    return (false);
  }

  if (isBusy()) {
    return (false);
  }

  readData();
  measurementStarted_ = false;
  return (true);
}

void AHT20::readData() {
  // Clear previous data
  sensorData_.temperature = 0;
  sensorData_.humidity = 0;

  if (Wire.requestFrom(ADDR_AHT20, (uint8_t)6) > 0) {
    Wire.read();  // Read and discard state

    uint32_t incoming = 0;
    incoming |= (uint32_t)Wire.read() << (8 * 2);
    incoming |= (uint32_t)Wire.read() << (8 * 1);
    uint8_t midByte = Wire.read();

    incoming |= midByte;
    sensorData_.humidity = incoming >> 4;

    sensorData_.temperature = (uint32_t)midByte << (8 * 2);
    sensorData_.temperature |= (uint32_t)Wire.read() << (8 * 1);
    sensorData_.temperature |= (uint32_t)Wire.read() << (8 * 0);

    // Need to get rid of data in bits > 20
    sensorData_.temperature = sensorData_.temperature & ~(0xFFF00000);

    // Mark data as fresh
    sensorQueried_.temperature = false;
    sensorQueried_.humidity = false;
  }
}

bool AHT20::triggerMeasurement() {
  Wire.beginTransmission(ADDR_AHT20);
  Wire.write(sfe_aht20_reg_measure);
  Wire.write((uint8_t)0x33);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() == 0) return true;
  return false;
}

bool AHT20::initialize() {
  Wire.beginTransmission(ADDR_AHT20);
  Wire.write(sfe_aht20_reg_initialize);
  Wire.write((uint8_t)0x08);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() == 0) return true;
  return false;
}

bool AHT20::isBusy() { return (getStatus() & (1 << 7)); }

bool AHT20::isCalibrated() { return (getStatus() & (1 << 3)); }

uint8_t AHT20::getStatus() {
  Wire.requestFrom(ADDR_AHT20, (uint8_t)1);
  if (Wire.available()) return (Wire.read());
  return (0);
}

bool AHT20::isConnected() {
  Wire.beginTransmission(ADDR_AHT20);
  if (Wire.endTransmission() == 0) return true;

  // If IC failed to respond, give it 20ms more for Power On Startup
  // Datasheet pg 7
  delay(20);

  Wire.beginTransmission(ADDR_AHT20);
  if (Wire.endTransmission() == 0) return true;

  return false;
}
