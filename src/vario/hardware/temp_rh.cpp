#include "hardware/temp_rh.h"

#include "hardware/Leaf_I2C.h"

#define DEBUG_TEMPRH 0  // flag for outputting debugf messages on UBS serial port

bool measurementStarted = false;

float ambientTemp = 0;      // calculated deg C
float ambientHumidity = 0;  // calculated % relative humidity

float tempRH_getTemp() { return ambientTemp; }

float tempRH_getHumidity() { return ambientHumidity; }

bool tempRH_init() {
  bool success = true;

  if (!tempRH_isConnected()) {
    success = false;

    if (DEBUG_TEMPRH) Serial.println("Temp_RH - AHT20 Temp Humidity sensor NOT FOUND!");

  } else {
    if (DEBUG_TEMPRH) Serial.println("Temp_RH - AHT20 Temp Humidity sensor FOUND!");

    // Wait 40 ms after power-on before reading temp or humidity. Datasheet pg 8
    delay(40);

    // Check if the calibrated bit is set. If not, init the sensor.
    if (!tempRH_isCalibrated()) {
      // Send 0xBE0800
      tempRH_initialize();

      // Immediately trigger a measurement. Send 0xAC3300
      tempRH_triggerMeasurement();

      delay(75);  // Wait for measurement to complete

      uint8_t counter = 0;
      while (tempRH_isBusy()) {
        delay(1);
        if (counter++ > 100) success = false;  // Give up after 100ms
      }

      // This calibration sequence is not completely proven. It's not clear how and when the cal bit
      // clears This seems to work but it's not easily testable
      if (!tempRH_isCalibrated()) success = false;
    }

    // Check that the cal bit has been set
    if (!tempRH_isCalibrated()) success = false;

    // Mark all datums as fresh (not read before)
    sensorQueried.temperature = true;
    sensorQueried.humidity = true;

    // Get a fresh initial measurement
    tempRH_update(1);
    delay(100);
    tempRH_update(2);
  }

  if (DEBUG_TEMPRH) {
    if (success)
      Serial.println("Temp_RH - SUCCESS: AHT20 Temp Humidity sensor calibrated!");
    else
      Serial.println("Temp_RH - FAIL: AHT20 Temp Humidity sensor not initialized/calibrated");
  }

  return success;
}

uint8_t currently_processing = false;

// don't call more often than every 1-2 seconds, or sensor will heat up slightly above ambient
void tempRH_update(uint8_t process_step) {
  // measurements must first be triggered, then >75ms later can be read
  if (process_step == 1) {
    if (!currently_processing)
      tempRH_triggerMeasurement();  // if we haven't yet processed a prior measurement, don't
                                    // trigger a new one
    currently_processing = true;
  } else if (process_step == 2) {
    if (!tempRH_isBusy()) {  // if busy, skip this and we'll try again next time
      tempRH_readData();
      ambientTemp = ((float)sensorData.temperature / 1048576) * 200 - 50;
      ambientTemp += TEMP_OFFSET;
      ambientHumidity = ((float)sensorData.humidity / 1048576) * 100;
      currently_processing = false;
      if (DEBUG_TEMPRH) {
        Serial.print("Temp_RH - Temp: ");
        Serial.print(ambientTemp);
        Serial.print("  Humidity: ");
        Serial.println(ambientHumidity);
      }
    } else {
      if (DEBUG_TEMPRH) Serial.println("Temp_RH - missed values due to sensor busy");
    }
  }
}

bool tempRH_softReset() {
  Wire.beginTransmission(ADDR_AHT20);
  Wire.write(sfe_aht20_reg_reset);
  if (Wire.endTransmission() == 0) return true;
  return false;
}

bool tempRH_available() {
  if (!measurementStarted) {
    tempRH_triggerMeasurement();
    measurementStarted = true;
    return (false);
  }

  if (tempRH_isBusy()) {
    return (false);
  }

  tempRH_readData();
  measurementStarted = false;
  return (true);
}

void tempRH_readData() {
  // Clear previous data
  sensorData.temperature = 0;
  sensorData.humidity = 0;

  if (Wire.requestFrom(ADDR_AHT20, (uint8_t)6) > 0) {
    Wire.read();  // Read and discard state

    uint32_t incoming = 0;
    incoming |= (uint32_t)Wire.read() << (8 * 2);
    incoming |= (uint32_t)Wire.read() << (8 * 1);
    uint8_t midByte = Wire.read();

    incoming |= midByte;
    sensorData.humidity = incoming >> 4;

    sensorData.temperature = (uint32_t)midByte << (8 * 2);
    sensorData.temperature |= (uint32_t)Wire.read() << (8 * 1);
    sensorData.temperature |= (uint32_t)Wire.read() << (8 * 0);

    // Need to get rid of data in bits > 20
    sensorData.temperature = sensorData.temperature & ~(0xFFF00000);

    // Mark data as fresh
    sensorQueried.temperature = false;
    sensorQueried.humidity = false;
  }
}

bool tempRH_triggerMeasurement() {
  Wire.beginTransmission(ADDR_AHT20);
  Wire.write(sfe_aht20_reg_measure);
  Wire.write((uint8_t)0x33);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() == 0) return true;
  return false;
}

bool tempRH_initialize() {
  Wire.beginTransmission(ADDR_AHT20);
  Wire.write(sfe_aht20_reg_initialize);
  Wire.write((uint8_t)0x08);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() == 0) return true;
  return false;
}

bool tempRH_isBusy() { return (tempRH_getStatus() & (1 << 7)); }

bool tempRH_isCalibrated() { return (tempRH_getStatus() & (1 << 3)); }

uint8_t tempRH_getStatus() {
  Wire.requestFrom(ADDR_AHT20, (uint8_t)1);
  if (Wire.available()) return (Wire.read());
  return (0);
}

bool tempRH_isConnected() {
  Wire.beginTransmission(ADDR_AHT20);
  if (Wire.endTransmission() == 0) return true;

  // If IC failed to respond, give it 20ms more for Power On Startup
  // Datasheet pg 7
  delay(20);

  Wire.beginTransmission(ADDR_AHT20);
  if (Wire.endTransmission() == 0) return true;

  return false;
}