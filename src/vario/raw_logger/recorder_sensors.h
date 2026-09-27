#pragma once

#include <Arduino.h>
#include <ICM_20948.h>
#include <TinyGPSPlus.h>
#include <cstdint>

#include "raw_logger/flight_tape.h"

namespace leaf::raw {

  class RecorderSensors {
   public:
    bool begin();
    void update();

    bool gpsLocked() const;
    int64_t gpsUtcUs();
    uint8_t gpsFixQuality() const { return gpsFixQuality_; }
    uint8_t satellites() { return gps_.satellites.isValid() ? gps_.satellites.value() : 0; }
    const uint16_t* baroCalibration() const { return baroCalibration_; }
    bool barometerReady() const { return barometerReady_; }
    bool imuReady() const { return imuReady_; }
    bool ambientReady() const { return ambientReady_; }
    uint32_t imuErrors() const { return imuErrors_; }

   private:
    enum class BaroState : uint8_t { Uninitialized, MeasuringTemperature, MeasuringPressure };
    enum class AmbientState : uint8_t { WaitingForPower, Idle, Measuring };

    void updateGps(uint64_t nowUs);
    void updateBarometer(uint64_t nowUs);
    void updateImu(uint64_t nowUs);
    void updateAmbient(uint64_t nowUs);
    void updatePower(uint64_t nowUs);
    bool beginBarometer();
    bool beginImu();
    void startBaroConversion(uint8_t command, BaroState nextState, uint64_t nowUs);
    uint32_t readBaroAdc();
    int32_t compensatedPressure(uint32_t d1, uint32_t d2) const;
    void finishGpsSentence(uint64_t nowUs);
    bool readAmbient(RawAmbientPayload& payload);

    TinyGPSPlus gps_;
    ICM_20948_I2C imu_;
    char nmea_[96] = {};
    uint8_t nmeaLength_ = 0;
    uint8_t gpsFixQuality_ = 0;
    int64_t lastTimeSyncSecond_ = -1;

    uint16_t baroCalibration_[6] = {};
    BaroState baroState_ = BaroState::Uninitialized;
    uint64_t baroConversionStartedUs_ = 0;
    uint32_t lastBaroTemperature_ = 0;
    uint16_t pressureSamplesSinceTemperature_ = 0;
    bool barometerReady_ = false;

    AmbientState ambientState_ = AmbientState::WaitingForPower;
    uint32_t ambientActionMs_ = 0;
    bool ambientReady_ = false;

    bool imuReady_ = false;
    uint32_t imuErrors_ = 0;
    uint32_t lastPowerMs_ = 0;
  };

  extern RecorderSensors recorderSensors;

}  // namespace leaf::raw
