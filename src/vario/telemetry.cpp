#include "telemetry.h"

#include <SD_MMC.h>

#include "Arduino.h"
#include "gps.h"
#include "log.h"
#include "settings.h"

bool Telemetry_t::begin() {
// Get the local time
#ifndef ENABLE_TELEMETRY
  return true;
#endif

  tm cal;
  if (!gps_getLocalDateTime(cal)) {
    return false;
  }

  // gps.altitude.value();

  // format with strftime format.  Eg FlightTrack_2025-01-08_2301
  char fileName[60];
  String formatString = "/Data/TestData_%F_%H%M%S.dat";
  strftime(fileName, sizeof(fileName), formatString.c_str(), &cal);

  // Open the file to write the telemetry
  file = SD_MMC.open(fileName, "wb", true);
  if (!file) {
    return false;
  }

  // Write the 32-bit version number 0x01 constant
  uint32_t version = 0x01;
  file.write((uint8_t*)&version, sizeof(version));
  file.flush();

  // Always write the Baro sensor calibrations at the start of the file recording
  // If we don't do this, subsequent data records will be be missing the calibration data
  dataChanged |= BARO_SENSOR_CALIBRATIONS_CHANGED;

  return true;
}

void Telemetry_t::end() {
  file.close();
}

void Telemetry_t::setGPSPosition(int32_t altitude, double latitude, double longitude) {
  // If the values are the same, don't update
  if (gpsPosition.altitude == altitude && gpsPosition.latitude == latitude &&
      gpsPosition.longitude == longitude) {
    return;
  }

  gpsPosition.altitude = altitude;
  gpsPosition.latitude = latitude;
  gpsPosition.longitude = longitude;
  dataChanged |= GPS_POSITION_CHANGED;
}

void Telemetry_t::setTemperatureHumidity(int32_t temperature, int32_t humidity) {
  temperatureHumidity.temperature = temperature;
  temperatureHumidity.humidity = humidity;
  dataChanged |= TEMPERATURE_HUMIDITY_CHANGED;
}

void Telemetry_t::setBaroPressure(int32_t d1) {
  baroPressure.d1 = d1;
  dataChanged |= BARO_PRESSURE_CHANGED;
}

void Telemetry_t::setBaroTemp(int32_t d2_t) {
  baroTemp.d2_t = d2_t;
  dataChanged |= BARO_TEMP_CHANGED;
}

void Telemetry_t::setBaroSensorCalibrations(uint16_t c_sens,
                                            uint16_t c_off,
                                            uint16_t c_tcs,
                                            uint16_t c_tco,
                                            uint16_t c_tref,
                                            uint16_t c_tempsens) {
  baroSensorCalibrations.c_sens = c_sens;
  baroSensorCalibrations.c_off = c_off;
  baroSensorCalibrations.c_tcs = c_tcs;
  baroSensorCalibrations.c_tco = c_tco;
  baroSensorCalibrations.c_tref = c_tref;
  baroSensorCalibrations.c_tempsens = c_tempsens;
  dataChanged |= BARO_SENSOR_CALIBRATIONS_CHANGED;
}

void Telemetry_t::setClimbRate(int32_t climbRate) {
  this->climbRate.climbRate = climbRate;
  dataChanged |= CLIMB_RATE_CHANGED;
}

void Telemetry_t::setIMUOrientation(double q1, double q2, double q3, int16_t accuracy) {
  imuOrientation.q1 = q1;
  imuOrientation.q2 = q2;
  imuOrientation.q3 = q3;
  imuOrientation.accuracy = accuracy;
  dataChanged |= IMU_ORIENTATION_CHANGED;
}

void Telemetry_t::setIMUAcceleration(int16_t x, int16_t y, int16_t z, int8_t accuracy) {
  imuAcceleration.x = x;
  imuAcceleration.y = y;
  imuAcceleration.z = z;
  imuAcceleration.accuracy = accuracy;
  dataChanged |= IMU_ACCELERATION_CHANGED;
}

void Telemetry_t::writeTelemetryRecord(uint32_t timestamp) {
#ifndef ENABLE_TELEMETRY
  return;
#endif
  // This should serialize the data structures and write them in the specified binary format

  // If there's no data to write, return
  if (dataChanged == 0 || !file) {
    return;
  }

  // Add the timestamp
  dataChanged |= TIMESTAMP_CHANGED;

  // Write the payloads header
  file.write((uint8_t*)&dataChanged, sizeof(dataChanged));

  // Write the timestamp
  if (dataChanged & TIMESTAMP_CHANGED) {
    file.write((uint8_t*)&timestamp, sizeof(timestamp));
  }

  // Write the GPS position if changed
  if (dataChanged & GPS_POSITION_CHANGED) {
    file.write((uint8_t*)&gpsPosition.altitude, sizeof(gpsPosition.altitude));
    file.write((uint8_t*)&gpsPosition.latitude, sizeof(gpsPosition.latitude));
    file.write((uint8_t*)&gpsPosition.longitude, sizeof(gpsPosition.longitude));
  }

  // Write the temperature and humidity if changed
  if (dataChanged & TEMPERATURE_HUMIDITY_CHANGED) {
    file.write((uint8_t*)&temperatureHumidity.temperature, sizeof(temperatureHumidity.temperature));
    file.write((uint8_t*)&temperatureHumidity.humidity, sizeof(temperatureHumidity.humidity));
  }

  // Write the baro pressure if changed
  if (dataChanged & BARO_PRESSURE_CHANGED) {
    file.write((uint8_t*)&baroPressure.d1, sizeof(baroPressure.d1));
  }

  // Write the baro temp if changed
  if (dataChanged & BARO_TEMP_CHANGED) {
    file.write((uint8_t*)&baroTemp.d2_t, sizeof(baroTemp.d2_t));
  }

  // Write the baro sensor calibrations if changed
  if (dataChanged & BARO_SENSOR_CALIBRATIONS_CHANGED) {
    file.write((uint8_t*)&baroSensorCalibrations.c_sens, sizeof(baroSensorCalibrations.c_sens));
    file.write((uint8_t*)&baroSensorCalibrations.c_off, sizeof(baroSensorCalibrations.c_off));
    file.write((uint8_t*)&baroSensorCalibrations.c_tcs, sizeof(baroSensorCalibrations.c_tcs));
    file.write((uint8_t*)&baroSensorCalibrations.c_tco, sizeof(baroSensorCalibrations.c_tco));
    file.write((uint8_t*)&baroSensorCalibrations.c_tref, sizeof(baroSensorCalibrations.c_tref));
    file.write((uint8_t*)&baroSensorCalibrations.c_tempsens,
               sizeof(baroSensorCalibrations.c_tempsens));
  }

  // Write the climb rate
  if (dataChanged & CLIMB_RATE_CHANGED) {
    file.write((uint8_t*)&climbRate.climbRate, sizeof(climbRate.climbRate));
  }

  // Write the IMU orientation if changed
  if (dataChanged & IMU_ORIENTATION_CHANGED) {
    file.write((uint8_t*)&imuOrientation.q1, sizeof(imuOrientation.q1));
    file.write((uint8_t*)&imuOrientation.q2, sizeof(imuOrientation.q2));
    file.write((uint8_t*)&imuOrientation.q3, sizeof(imuOrientation.q3));
    file.write((uint8_t*)&imuOrientation.accuracy, sizeof(imuOrientation.accuracy));
  }

  // Write the IMU acceleration if changed
  if (dataChanged & IMU_ACCELERATION_CHANGED) {
    file.write((uint8_t*)&imuAcceleration.x, sizeof(imuAcceleration.x));
    file.write((uint8_t*)&imuAcceleration.y, sizeof(imuAcceleration.y));
    file.write((uint8_t*)&imuAcceleration.z, sizeof(imuAcceleration.z));
    file.write((uint8_t*)&imuAcceleration.accuracy, sizeof(imuAcceleration.accuracy));
  }

  file.flush();

  // Clear the data changed flags
  dataChanged = 0;
}

Telemetry_t Telemetry;