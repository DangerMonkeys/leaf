#pragma once

#include "files.h"

/*
 * # Telemetry Logging
 * Telemetry logging uses a binary file format to log telemetry data.
 * This is to ensure that the data is logged as quickly and as smallest as possible.
 *
 * ## File Format
 * The binary file log will include a header of the Version of the file format (currently 0x1).
 * followed by a "payload header" containing a bitfield of ordered telemetry payloads.
 * Each of the telemetry type payloads are written in the order shown below
 *
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 * | Version                                                       |
 * ----------------------------------------------------------------|
 * |                    Payload Header                             |
 * ----------------------------------------------------------------|
 * |                     (payloads...)                             |
 * ----------------------------------------------------------------|
 *
 * ## Payloads (in order)
 *
 *
 * ### Timestamp (bit 1)
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 * |                    Timestamp (micros uint32_t)                |
 * ----------------------------------------------------------------|
 *
 * ### GPS Position (bit 2)
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 * |                    Altitude  (int32_t)                        |
 * ----------------------------------------------------------------|
 * |                          Lat                                  |
 * |                   (double, 64 bits)                           |
 * ----------------------------------------------------------------|
 * |                          Long                                 |
 * |                   (double, 64 bits)                           |
 * ----------------------------------------------------------------|
 *
 * ### Temperature / Humidity (bit 3)
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 * |                    Temp  (int32_t)                            |
 * ----------------------------------------------------------------|
 * |                    Humidity (int32_t)                         |
 * ----------------------------------------------------------------|
 *
 * ### Baro Pressure (bit 4)
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 * |        D1 (digital pressure value)  (int32_t)                 |
 * ----------------------------------------------------------------|
 *
 * ### Baro Temp (bit 5)
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 * |        D2_T (digital pressure value)  (int32_t)               |
 * ----------------------------------------------------------------|
 *
 * ### Baro Sensor Calibrations (bit 6)
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 *      uint16_t C_SENS            |         uint16_t C_OFF        |
 * ----------------------------------------------------------------|
 *      uint16_t C_TCS             |         uint16_t C_TCO        |
 * ----------------------------------------------------------------|
 *      uint16_t C_TREF            |         uint16_t C_TEMPSENS   |
 * ----------------------------------------------------------------|
 *
 * ### Climb Rate (bit 7)
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 * |                      int32_t climb_rate                       |
 * ----------------------------------------------------------------|
 *
 *
 * ### IMU Orientation Quaternion 9-dof (bit 8)
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 * |                          q1                                   |
 * |                   (double, 64 bits)                           |
 * ----------------------------------------------------------------|
 * |                          q2                                   |
 * |                   (double, 64 bits)                           |
 * ----------------------------------------------------------------|
 * |                          q3                                   |
 * |                   (double, 64 bits)                           |
 * ----------------------------------------------------------------|
 * |      Accuracy (int16_5)       |                               |
 *
 * ### IMU Acceleration (bit 9)
 * 1               8               16                              32
 * ----------------------------------------------------------------|
 *      int16_t x                  |          int16_t y            |
 * ----------------------------------------------------------------|
 *      int16_t z                  |int8_t accuracy|               |
 * ----------------------------------------------------------------|
 *
 */

struct T_Timestamp {
  uint32_t micros;
};

struct T_GPSPosition {
  int32_t altitude;
  int64_t latitude;
  int64_t longitude;
};

struct T_TemperatureHumidity {
  int32_t temperature;
  int32_t humidity;
};

struct T_BaroPressure {
  int32_t d1;
};

struct T_BaroTemp {
  int32_t d2_t;
};

struct T_BaroSensorCalibrations {
  uint16_t c_sens;
  uint16_t c_off;
  uint16_t c_tcs;
  uint16_t c_tco;
  uint16_t c_tref;
  uint16_t c_tempsens;
};

struct T_ClimbRate {
  int32_t climbRate;
};

struct T_IMUOrientation {
  double q1;
  double q2;
  double q3;
  int16_t accuracy;
};

struct T_IMUAcceleration {
  int16_t x;
  int16_t y;
  int16_t z;
  int8_t accuracy;
};

class Telemetry_t {
 public:
  Telemetry_t() : dataChanged(0) {}
  bool begin();
  void end();

  void setGPSPosition(int32_t altitude, double latitude, double longitude);
  void setTemperatureHumidity(int32_t temperature, int32_t humidity);
  void setBaroPressure(int32_t d1);
  void setBaroTemp(int32_t d2_t);
  void setBaroSensorCalibrations(uint16_t c_sens,
                                 uint16_t c_off,
                                 uint16_t c_tcs,
                                 uint16_t c_tco,
                                 uint16_t c_tref,
                                 uint16_t c_tempsens);
  void setIMUOrientation(double q1, double q2, double q3, int16_t accuracy);
  void setIMUAcceleration(int16_t x, int16_t y, int16_t z, int8_t accuracy);
  void setClimbRate(int32_t climbRate);
  void writeTelemetryRecord(uint32_t timestamp);

 private:
  File file;
  T_GPSPosition gpsPosition;
  T_TemperatureHumidity temperatureHumidity;
  T_BaroPressure baroPressure;
  T_BaroTemp baroTemp;
  T_BaroSensorCalibrations baroSensorCalibrations;
  T_ClimbRate climbRate;
  T_IMUOrientation imuOrientation;
  T_IMUAcceleration imuAcceleration;

  uint32_t dataChanged;

  enum DataChangedFlags {
    TIMESTAMP_CHANGED = 1 << 0,                 // Bit 1
    GPS_POSITION_CHANGED = 1 << 1,              // Bit 2
    TEMPERATURE_HUMIDITY_CHANGED = 1 << 2,      // Bit 3
    BARO_PRESSURE_CHANGED = 1 << 3,             // Bit 4
    BARO_TEMP_CHANGED = 1 << 4,                 // Bit 5
    BARO_SENSOR_CALIBRATIONS_CHANGED = 1 << 5,  // Bit 6
    CLIMB_RATE_CHANGED = 1 << 6,                // Bit 7
    IMU_ORIENTATION_CHANGED = 1 << 7,           // Bit 8
    IMU_ACCELERATION_CHANGED = 1 << 8           // Bit 9
  };
};

extern Telemetry_t Telemetry;