#include "raw_logger/recorder_sensors.h"

#include <Wire.h>
#include <esp_timer.h>
#include <cstring>

#include "hardware/configuration.h"
#include "hardware/io_pins.h"

namespace leaf::raw {

  RecorderSensors recorderSensors;

  namespace {
    constexpr uint8_t BARO_ADDRESS = 0x77;
    constexpr uint8_t BARO_RESET = 0x1E;
    constexpr uint8_t BARO_CONVERT_PRESSURE = 0x48;
    constexpr uint8_t BARO_CONVERT_TEMPERATURE = 0x58;
    constexpr uint32_t BARO_CONVERSION_US = 9040;
    constexpr uint16_t BARO_PRESSURES_PER_TEMPERATURE = 100;

    constexpr uint8_t AHT20_ADDRESS = 0x38;
    constexpr uint8_t AHT20_INITIALIZE = 0xBE;
    constexpr uint8_t AHT20_MEASURE = 0xAC;

    constexpr int IMU_DMP_INTERVAL = 2;
    constexpr uint8_t MAX_IMU_PACKETS_PER_UPDATE = 16;

    constexpr uint8_t BATTERY_SENSE_PIN = 1;

    int64_t daysFromCivil(int year, unsigned month, unsigned day) {
      year -= month <= 2;
      const int era = (year >= 0 ? year : year - 399) / 400;
      const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
      const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
      const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
      return era * 146097 + static_cast<int>(dayOfEra) - 719468;
    }

    bool sendI2cCommand(uint8_t address, uint8_t command) {
      Wire.beginTransmission(address);
      Wire.write(command);
      return Wire.endTransmission() == 0;
    }
  }  // namespace

  bool RecorderSensors::begin() {
    pinMode(BATTERY_SENSE_PIN, INPUT);
#ifdef ISET
    pinMode(ISET, INPUT);
#endif

    Serial0.begin(115200);
    barometerReady_ = beginBarometer();
    imuReady_ = beginImu();
    ambientActionMs_ = millis();
    return barometerReady_ && imuReady_;
  }

  bool RecorderSensors::beginBarometer() {
    if (!sendI2cCommand(BARO_ADDRESS, BARO_RESET)) return false;
    delay(4);
    for (uint8_t index = 0; index < 6; ++index) {
      const uint8_t command = 0xA2 + index * 2;
      if (!sendI2cCommand(BARO_ADDRESS, command)) return false;
      if (Wire.requestFrom(BARO_ADDRESS, static_cast<uint8_t>(2)) != 2) return false;
      baroCalibration_[index] = static_cast<uint16_t>(Wire.read()) << 8;
      baroCalibration_[index] |= Wire.read();
      if (baroCalibration_[index] == 0) return false;
    }
    startBaroConversion(BARO_CONVERT_TEMPERATURE, BaroState::MeasuringTemperature,
                        esp_timer_get_time());
    return true;
  }

  bool RecorderSensors::beginImu() {
    bool connected = false;
    for (uint8_t attempt = 0; attempt < 5 && !connected; ++attempt) {
      imu_.begin(Wire, 0);
      connected = imu_.status == ICM_20948_Stat_Ok;
      if (!connected) delay(100);
    }
    if (!connected) return false;

    bool success = imu_.initializeDMP() == ICM_20948_Stat_Ok;
    success &= imu_.enableDMPSensor(INV_ICM20948_SENSOR_RAW_ACCELEROMETER) == ICM_20948_Stat_Ok;
    success &= imu_.enableDMPSensor(INV_ICM20948_SENSOR_RAW_GYROSCOPE) == ICM_20948_Stat_Ok;
    success &=
        imu_.enableDMPSensor(INV_ICM20948_SENSOR_MAGNETIC_FIELD_UNCALIBRATED) == ICM_20948_Stat_Ok;
    success &= imu_.enableDMPSensor(INV_ICM20948_SENSOR_ORIENTATION) == ICM_20948_Stat_Ok;
    success &= imu_.setDMPODRrate(DMP_ODR_Reg_Accel, IMU_DMP_INTERVAL) == ICM_20948_Stat_Ok;
    success &= imu_.setDMPODRrate(DMP_ODR_Reg_Gyro, IMU_DMP_INTERVAL) == ICM_20948_Stat_Ok;
    success &= imu_.setDMPODRrate(DMP_ODR_Reg_Cpass, IMU_DMP_INTERVAL) == ICM_20948_Stat_Ok;
    success &= imu_.setDMPODRrate(DMP_ODR_Reg_Quat9, IMU_DMP_INTERVAL) == ICM_20948_Stat_Ok;
    success &= imu_.enableFIFO() == ICM_20948_Stat_Ok;
    success &= imu_.enableDMP() == ICM_20948_Stat_Ok;
    success &= imu_.resetDMP() == ICM_20948_Stat_Ok;
    success &= imu_.resetFIFO() == ICM_20948_Stat_Ok;
    return success;
  }

  void RecorderSensors::update() {
    const uint64_t nowUs = esp_timer_get_time();
    updateGps(nowUs);
    if (barometerReady_) updateBarometer(nowUs);
    if (imuReady_) updateImu(nowUs);
    updateAmbient(nowUs);
    updatePower(nowUs);
  }

  void RecorderSensors::updateGps(uint64_t nowUs) {
    while (Serial0.available()) {
      const char c = static_cast<char>(Serial0.read());
      gps_.encode(c);
      if (c == '\r' || c == '\n') {
        if (nmeaLength_) finishGpsSentence(nowUs);
        continue;
      }
      if (nmeaLength_ + 1 < sizeof(nmea_)) {
        nmea_[nmeaLength_++] = c;
      } else {
        nmeaLength_ = 0;
      }
    }
  }

  void RecorderSensors::finishGpsSentence(uint64_t nowUs) {
    nmea_[nmeaLength_] = '\0';
    if (nmeaLength_ > 6 && nmea_[0] == '$' && nmea_[3] == 'G' && nmea_[4] == 'G' &&
        nmea_[5] == 'A') {
      const char* field = nmea_;
      for (uint8_t comma = 0; comma < 6 && field; ++comma) field = strchr(field + 1, ',');
      if (field && field[1] >= '0' && field[1] <= '9') gpsFixQuality_ = field[1] - '0';
    }

    if (flightTape.recording()) {
      flightTape.appendBytes(RecordType::RawGpsNmea, nmea_, nmeaLength_, nowUs);
      const int64_t utcUs = gpsUtcUs();
      if (utcUs > 0) {
        const int64_t second = utcUs / 1000000;
        if (second != lastTimeSyncSecond_) {
          const TimeSyncPayload sync{utcUs, gpsFixQuality_, satellites(), {}};
          flightTape.append(RecordType::TimeSync, sync, nowUs);
          lastTimeSyncSecond_ = second;
        }
      }
    }
    nmeaLength_ = 0;
  }

  bool RecorderSensors::gpsLocked() const {
    constexpr uint32_t MAX_AGE_MS = 3000;
    return gps_.date.isValid() && gps_.time.isValid() && gps_.location.isValid() &&
           gps_.date.age() <= MAX_AGE_MS && gps_.time.age() <= MAX_AGE_MS &&
           gps_.location.age() <= MAX_AGE_MS && gpsFixQuality_ > 0;
  }

  int64_t RecorderSensors::gpsUtcUs() {
    if (!gps_.date.isValid() || !gps_.time.isValid()) return 0;
    const int year = gps_.date.year();
    const unsigned month = gps_.date.month();
    const unsigned day = gps_.date.day();
    if (year < 1980 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
    const int64_t seconds = daysFromCivil(year, month, day) * 86400LL + gps_.time.hour() * 3600LL +
                            gps_.time.minute() * 60LL + gps_.time.second();
    return seconds * 1000000LL + gps_.time.centisecond() * 10000LL;
  }

  void RecorderSensors::startBaroConversion(uint8_t command, BaroState nextState, uint64_t nowUs) {
    if (sendI2cCommand(BARO_ADDRESS, command)) {
      baroState_ = nextState;
      baroConversionStartedUs_ = nowUs;
    }
  }

  uint32_t RecorderSensors::readBaroAdc() {
    if (!sendI2cCommand(BARO_ADDRESS, 0x00)) return 0;
    if (Wire.requestFrom(BARO_ADDRESS, static_cast<uint8_t>(3)) != 3) return 0;
    uint32_t value = static_cast<uint32_t>(Wire.read()) << 16;
    value |= static_cast<uint32_t>(Wire.read()) << 8;
    value |= Wire.read();
    return value;
  }

  void RecorderSensors::updateBarometer(uint64_t nowUs) {
    if (nowUs - baroConversionStartedUs_ < BARO_CONVERSION_US) return;
    const uint32_t adc = readBaroAdc();
    if (!adc) {
      startBaroConversion(baroState_ == BaroState::MeasuringTemperature ? BARO_CONVERT_TEMPERATURE
                                                                        : BARO_CONVERT_PRESSURE,
                          baroState_, nowUs);
      return;
    }

    if (baroState_ == BaroState::MeasuringTemperature) {
      lastBaroTemperature_ = adc;
      const RawScalarPayload payload{adc};
      flightTape.append(RecordType::RawBaroTemperature, payload, nowUs);
      pressureSamplesSinceTemperature_ = 0;
      startBaroConversion(BARO_CONVERT_PRESSURE, BaroState::MeasuringPressure, nowUs);
      return;
    }

    const RawScalarPayload raw{adc};
    flightTape.append(RecordType::RawBaroPressure, raw, nowUs);
    if (lastBaroTemperature_) {
      const BusPressurePayload pressure{compensatedPressure(adc, lastBaroTemperature_)};
      flightTape.append(RecordType::BusPressure, pressure, nowUs);
    }
    if (++pressureSamplesSinceTemperature_ >= BARO_PRESSURES_PER_TEMPERATURE) {
      startBaroConversion(BARO_CONVERT_TEMPERATURE, BaroState::MeasuringTemperature, nowUs);
    } else {
      startBaroConversion(BARO_CONVERT_PRESSURE, BaroState::MeasuringPressure, nowUs);
    }
  }

  int32_t RecorderSensors::compensatedPressure(uint32_t d1, uint32_t d2) const {
    const int64_t dT = static_cast<int64_t>(d2) - static_cast<int64_t>(baroCalibration_[4]) * 256;
    int64_t temperature = 2000 + (dT * baroCalibration_[5]) / (1LL << 23);
    int64_t offset = static_cast<int64_t>(baroCalibration_[1]) * (1LL << 16) +
                     (static_cast<int64_t>(baroCalibration_[3]) * dT) / (1LL << 7);
    int64_t sensitivity = static_cast<int64_t>(baroCalibration_[0]) * (1LL << 15) +
                          (static_cast<int64_t>(baroCalibration_[2]) * dT) / (1LL << 8);
    int64_t temperature2 = 0;
    int64_t offset2 = 0;
    int64_t sensitivity2 = 0;
    if (temperature < 2000) {
      temperature2 = (dT * dT) / (1LL << 31);
      offset2 = 5 * (temperature - 2000) * (temperature - 2000) / 2;
      sensitivity2 = 5 * (temperature - 2000) * (temperature - 2000) / 4;
    }
    if (temperature < -1500) {
      offset2 += 7 * (temperature + 1500) * (temperature + 1500);
      sensitivity2 += 11 * (temperature + 1500) * (temperature + 1500) / 2;
    }
    temperature -= temperature2;
    offset -= offset2;
    sensitivity -= sensitivity2;
    return static_cast<int32_t>(((static_cast<int64_t>(d1) * sensitivity) / (1LL << 21) - offset) /
                                (1LL << 15));
  }

  void RecorderSensors::updateImu(uint64_t nowUs) {
    for (uint8_t packet = 0; packet < MAX_IMU_PACKETS_PER_UPDATE; ++packet) {
      icm_20948_DMP_data_t data{};
      imu_.readDMPdataFromFIFO(&data);
      if (imu_.status == ICM_20948_Stat_FIFONoDataAvail) return;
      if (imu_.status == ICM_20948_Stat_FIFOIncompleteData) {
        imuErrors_++;
        imu_.resetFIFO();
        return;
      }
      if (imu_.status != ICM_20948_Stat_Ok && imu_.status != ICM_20948_Stat_FIFOMoreDataAvail) {
        imuErrors_++;
        return;
      }

      BusMotionPayload motion{};
      if (data.header & DMP_header_bitmap_Accel) {
        const RawVectorPayload accel{data.Raw_Accel.Data.X, data.Raw_Accel.Data.Y,
                                     data.Raw_Accel.Data.Z};
        flightTape.append(RecordType::RawImuAccel, accel, nowUs);
        motion.hasAcceleration = 1;
        motion.ax = static_cast<double>(accel.x) / 8192.0;
        motion.ay = static_cast<double>(accel.y) / 8192.0;
        motion.az = static_cast<double>(accel.z) / 8192.0;
      }
      if (data.header & DMP_header_bitmap_Gyro) {
        const RawGyroPayload gyro{data.Raw_Gyro.Data.X,     data.Raw_Gyro.Data.Y,
                                  data.Raw_Gyro.Data.Z,     data.Raw_Gyro.Data.BiasX,
                                  data.Raw_Gyro.Data.BiasY, data.Raw_Gyro.Data.BiasZ};
        flightTape.append(RecordType::RawImuGyro, gyro, nowUs);
      }
      if (data.header & DMP_header_bitmap_Compass) {
        const RawVectorPayload mag{data.Compass.Data.X, data.Compass.Data.Y, data.Compass.Data.Z};
        flightTape.append(RecordType::RawImuMag, mag, nowUs);
      }
      if (data.header & DMP_header_bitmap_Quat9) {
        const RawQuaternionPayload quat{data.Quat9.Data.Q1, data.Quat9.Data.Q2, data.Quat9.Data.Q3,
                                        data.Quat9.Data.Accuracy};
        flightTape.append(RecordType::RawImuQuaternion, quat, nowUs);
        motion.hasOrientation = 1;
        motion.qx = static_cast<double>(quat.q1) / 1073741824.0;
        motion.qy = static_cast<double>(quat.q2) / 1073741824.0;
        motion.qz = static_cast<double>(quat.q3) / 1073741824.0;
      }
      if (motion.hasAcceleration || motion.hasOrientation) {
        flightTape.append(RecordType::BusMotion, motion, nowUs);
      }
      if (imu_.status != ICM_20948_Stat_FIFOMoreDataAvail) return;
      nowUs = esp_timer_get_time();
    }
    imuErrors_++;
    imu_.resetFIFO();
  }

  void RecorderSensors::updateAmbient(uint64_t nowUs) {
    const uint32_t nowMs = millis();
    if (ambientState_ == AmbientState::WaitingForPower) {
      if (nowMs - ambientActionMs_ < 40) return;
      Wire.beginTransmission(AHT20_ADDRESS);
      Wire.write(AHT20_INITIALIZE);
      Wire.write(static_cast<uint8_t>(0x08));
      Wire.write(static_cast<uint8_t>(0x00));
      Wire.endTransmission();
      ambientState_ = AmbientState::Idle;
      ambientActionMs_ = nowMs - 1000;
    }
    if (ambientState_ == AmbientState::Idle && nowMs - ambientActionMs_ >= 1000) {
      Wire.beginTransmission(AHT20_ADDRESS);
      Wire.write(AHT20_MEASURE);
      Wire.write(static_cast<uint8_t>(0x33));
      Wire.write(static_cast<uint8_t>(0x00));
      if (Wire.endTransmission() == 0) {
        ambientState_ = AmbientState::Measuring;
        ambientActionMs_ = nowMs;
      }
      return;
    }
    if (ambientState_ != AmbientState::Measuring || nowMs - ambientActionMs_ < 80) return;

    RawAmbientPayload raw{};
    if (readAmbient(raw)) {
      ambientReady_ = true;
      flightTape.append(RecordType::RawAmbient, raw, nowUs);
      const BusAmbientPayload ambient{
          static_cast<float>(raw.temperature) / 1048576.0F * 200.0F - 53.0F,
          static_cast<float>(raw.humidity) / 1048576.0F * 100.0F};
      flightTape.append(RecordType::BusAmbient, ambient, nowUs);
    }
    ambientState_ = AmbientState::Idle;
    ambientActionMs_ = nowMs;
  }

  bool RecorderSensors::readAmbient(RawAmbientPayload& payload) {
    if (Wire.requestFrom(AHT20_ADDRESS, static_cast<uint8_t>(6)) != 6) return false;
    const uint8_t status = Wire.read();
    if (status & 0x80) return false;
    const uint8_t b1 = Wire.read();
    const uint8_t b2 = Wire.read();
    const uint8_t b3 = Wire.read();
    const uint8_t b4 = Wire.read();
    const uint8_t b5 = Wire.read();
    payload.humidity =
        (static_cast<uint32_t>(b1) << 12) | (static_cast<uint32_t>(b2) << 4) | (b3 >> 4);
    payload.temperature =
        (static_cast<uint32_t>(b3 & 0x0F) << 16) | (static_cast<uint32_t>(b4) << 8) | b5;
    return true;
  }

  void RecorderSensors::updatePower(uint64_t nowUs) {
    if (millis() - lastPowerMs_ < 1000) return;
    lastPowerMs_ = millis();
    RawPowerPayload power{};
    power.batteryMv = analogReadMilliVolts(BATTERY_SENSE_PIN) * 69 / 41;
#ifdef ISET
    power.chargeCurrentMa = analogReadMilliVolts(ISET) * 400 / 1100;
#endif
    power.charging = !ioexDigitalRead(POWER_CHARGE_GOOD_IOEX, POWER_CHARGE_GOOD);
    flightTape.append(RecordType::RawPower, power, nowUs);
  }

}  // namespace leaf::raw
