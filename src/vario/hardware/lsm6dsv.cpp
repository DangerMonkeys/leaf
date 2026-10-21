#include "hardware/configuration.h"
#ifdef USE_LSM6DSV

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <cmath>
#include "diagnostics/self_test/selfTest.h"
#include "hardware/lsm6dsv.h"
#include "instruments/baro.h"
#include "instruments/imu.h"
#include "math/gravity_projection.h"

namespace {
  constexpr uint8_t ADDRESS = 0x6A;
  constexpr uint32_t WARMUP_MS = 3000;
  static_assert(SFLP_ACCEL_FULL_SCALE_G == 8.0,
                "Update the operating full scale and FIFO conversion together");
#ifdef LEAF_IMU_STARTUP_TRACE
  void startupEvent(const char* event, uint32_t start) {
    const uint32_t now = millis();
    Serial.printf("IMU_STARTUP_EVENT,%lu,%lu,%s\n", (unsigned long)now,
                  (unsigned long)(now - start), event);
  }
#endif
  // Two FIFO words per 60 Hz sample. Never consume a long backlog as current data.
  constexpr uint16_t MAX_FIFO_WORDS = 16;
  struct BiasRecord {
    uint32_t version;
    float x, y, z;
  };
  constexpr uint32_t BIAS_VERSION = 0x3280001;
  int16_t axis(const uint8_t* p) {
    return static_cast<int16_t>(uint16_t(p[0]) | uint16_t(p[1]) << 8);
  }
  bool validBias(const BiasRecord& b) {
    return b.version == BIAS_VERSION && std::isfinite(b.x) && std::isfinite(b.y) &&
           std::isfinite(b.z) && fabs(b.x) < 5 && fabs(b.y) < 5 && fabs(b.z) < 5;
  }
}  // namespace

int32_t LSM6DSV::read(void* handle, uint8_t reg, uint8_t* data, uint16_t len) {
  auto* self = static_cast<LSM6DSV*>(handle);
  Wire.beginTransmission(ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDRESS, size_t(len)) != len) {
    self->ioErrors_++;
    return -1;
  }
  for (uint16_t i = 0; i < len; ++i) data[i] = Wire.read();
  return 0;
}

int32_t LSM6DSV::write(void* handle, uint8_t reg, const uint8_t* data, uint16_t len) {
  Wire.beginTransmission(ADDRESS);
  Wire.write(reg);
  Wire.write(data, len);
  if (Wire.endTransmission() != 0) {
    static_cast<LSM6DSV*>(handle)->ioErrors_++;
    return -1;
  }
  return 0;
}

void LSM6DSV::invalidate() {
  pending_ = MotionUpdate(0);
  haveCounter_ = false;
  pendingClipped_ = false;
  clippedSincePublish_ = false;
  climbSum_ = climbSumSquares_ = 0;
  climbSamples_ = 0;
  // Also invalidates instruments when hardware publishing is disconnected for replay.
  if (bus_) {
    MotionUpdate m(millis());
    m.reset = true;
    bus_->receive(m);
  }
}

void LSM6DSV::init() {
  ctx_.handle = this;
  ctx_.read_reg = read;
  ctx_.write_reg = write;
  ctx_.mdelay = [](uint32_t ms) { delay(ms); };
  delay(10);
  wake();
}

bool LSM6DSV::configure() {
#ifdef LEAF_IMU_STARTUP_TRACE
  traceStarted_ = millis();
  traceSeen_ = 0;
  startupEvent("configure_begin", traceStarted_);
#endif
  invalidate();
  uint8_t id = 0;
  if (lsm6dsv_device_id_get(&ctx_, &id) != 0 || id != LSM6DSV_ID) {
    Serial.printf("LSM6DSV: WHO_AM_I=0x%02X, expected 0x%02X at 0x6A\n", id, LSM6DSV_ID);
    return false;
  }
  // POR also clears all interrupt routes and the sensor hub. No magnetometer access.
  if (lsm6dsv_sw_por(&ctx_) != 0) return false;
#ifdef LEAF_IMU_STARTUP_TRACE
  startupEvent("reset_done", traceStarted_);
#endif
  if (lsm6dsv_block_data_update_set(&ctx_, 1) != 0 ||
      lsm6dsv_xl_full_scale_set(&ctx_, LSM6DSV_8g) != 0 ||
      lsm6dsv_gy_full_scale_set(&ctx_, LSM6DSV_2000dps) != 0 ||
      lsm6dsv_xl_data_rate_set(&ctx_, LSM6DSV_ODR_AT_60Hz) != 0 ||
      lsm6dsv_gy_data_rate_set(&ctx_, LSM6DSV_ODR_AT_60Hz) != 0 ||
      lsm6dsv_sflp_data_rate_set(&ctx_, LSM6DSV_SFLP_60Hz) != 0 ||
      lsm6dsv_sflp_game_rotation_set(&ctx_, 1) != 0)
    return false;
  lsm6dsv_xl_full_scale_t fullScale;
  if (lsm6dsv_xl_full_scale_get(&ctx_, &fullScale) != 0 || fullScale != LSM6DSV_8g) return false;
#ifdef LEAF_IMU_STARTUP_TRACE
  startupEvent("sensors_enabled", traceStarted_);
#endif

  Preferences prefs;
  BiasRecord bias{};
  calibrated_ = false;
  if (prefs.begin("imu328", true)) {
    calibrated_ = prefs.getBytesLength("gyroBias") == sizeof(bias) &&
                  prefs.getBytes("gyroBias", &bias, sizeof(bias)) == sizeof(bias) &&
                  validBias(bias);
    prefs.end();
  }
  // Seed even an uncommissioned board with zero rather than learning a gyro bias in flight.
  lsm6dsv_sflp_gbias_t seed{};
  if (calibrated_) {
    seed.gbias_x = bias.x;
    seed.gbias_y = bias.y;
    seed.gbias_z = bias.z;
  }
  if (lsm6dsv_sflp_game_gbias_set(&ctx_, &seed) != 0) return false;
#ifdef LEAF_IMU_STARTUP_TRACE
  startupEvent(calibrated_ ? "saved_bias_seeded" : "zero_bias_seeded", traceStarted_);
#endif
  lsm6dsv_fifo_sflp_raw_t batch{};
  batch.gravity = 1;
  if (lsm6dsv_fifo_sflp_batch_set(&ctx_, batch) != 0 ||
      lsm6dsv_fifo_xl_batch_set(&ctx_, LSM6DSV_XL_BATCHED_AT_60Hz) != 0 ||
      lsm6dsv_fifo_mode_set(&ctx_, LSM6DSV_STREAM_MODE) != 0)
    return false;
  started_ = lastPair_ = lastPoll_ = millis();
#ifdef LEAF_IMU_STARTUP_TRACE
  startupEvent("fifo_enabled", traceStarted_);
  Serial.println(
      "IMU_STARTUP_HEADER,t_ms,elapsed_ms,ax_g,ay_g,az_g,gx_g,gy_g,gz_g,vertical_g,"
      "pressure_pa,pressure_t_ms,baro_ready,published,filter_initialized,climb_valid,"
      "filter_samples,climb_mps,bias_mps2,fifo_words,clipped_since_update,bias_learning_allowed");
#endif
  status_ = "Warming up";
  Serial.printf("LSM6DSV: id=0x%02X, 60Hz, +/-8g, SFLP gravity, gyro bias %s\n", id,
                calibrated_ ? "restored" : "NOT commissioned (zero seed)");
  return true;
}

bool LSM6DSV::powerDown() {
  // Try every operation even after an error; never leave a self-test stimulus enabled.
  int result = lsm6dsv_mem_bank_set(&ctx_, LSM6DSV_MAIN_MEM_BANK);
  result |= lsm6dsv_xl_self_test_set(&ctx_, LSM6DSV_XL_ST_DISABLE);
  result |= lsm6dsv_gy_self_test_set(&ctx_, LSM6DSV_GY_ST_DISABLE);
  result |= lsm6dsv_sflp_game_rotation_set(&ctx_, 0);
  result |= lsm6dsv_fifo_sflp_batch_set(&ctx_, {});
  result |= lsm6dsv_fifo_mode_set(&ctx_, LSM6DSV_BYPASS_MODE);
  result |= lsm6dsv_xl_data_rate_set(&ctx_, LSM6DSV_ODR_OFF);
  result |= lsm6dsv_gy_data_rate_set(&ctx_, LSM6DSV_ODR_OFF);
  return result == 0;
}

void LSM6DSV::sleep() {
  if (testResult_ == TestResult::Running) testResult_ = TestResult::Fail;
  sleeping_ = true;
  running_ = false;
  invalidate();
  status_ = powerDown() ? "Sleeping" : "Sleep I2C fault";
  Serial.printf("LSM6DSV: %s\n", status_);
}

void LSM6DSV::wake() {
  sleeping_ = false;
  retries_ = 0;
  running_ = configure();
  if (!running_) fault("IMU init fault");
}

void LSM6DSV::fault(const char* reason) {
  running_ = false;
  status_ = reason;
  resets_++;
  invalidate();
  powerDown();
  retryAt_ = millis();
  Serial.printf("LSM6DSV: t=%lu %s (I2C errors=%lu, retries=%u/3)\n", (unsigned long)millis(),
                reason, (unsigned long)ioErrors_, retries_);
}

void LSM6DSV::update() {
  updates_++;
  if (sleeping_) return;
  if (testResult_ == TestResult::Running) {
    updateSelfTest();
    return;
  }
  if (!running_) {
    if (retries_ < 3 && uint32_t(millis() - retryAt_) >= 2000) {
      ++retries_;
      running_ = configure();
      if (!running_) fault("IMU retry failed");
    }
    return;
  }
  const uint32_t now = millis();
  if (uint32_t(now - lastPoll_) > 200) {
    fault("IMU polling gap");
    return;
  }
  lastPoll_ = now;
  lsm6dsv_fifo_status_t fifo{};
  if (lsm6dsv_fifo_status_get(&ctx_, &fifo) != 0) {
    fault("FIFO I2C fault");
    return;
  }
  if (fifo.fifo_ovr || fifo.fifo_full || fifo.fifo_level > MAX_FIFO_WORDS) {
    fault("FIFO overrun/backlog");
    return;
  }
  if (!fifo.fifo_level) ++empty_;
  MotionUpdate latest(now);
  for (uint16_t i = 0; i < fifo.fifo_level; ++i) {
    lsm6dsv_fifo_out_raw_t word{};
    if (lsm6dsv_fifo_out_raw_get(&ctx_, &word) != 0) {
      fault("FIFO read fault");
      return;
    }
    ++packets_;
    if (word.tag != LSM6DSV_XL_NC_TAG && word.tag != LSM6DSV_SFLP_GRAVITY_VECTOR_TAG) continue;
    if (!haveCounter_ || word.cnt != counter_) {
      if (pending_.hasAcceleration != pending_.hasGravity) ++mismatched_;
      pending_ = MotionUpdate(now);
      pendingClipped_ = false;
      counter_ = word.cnt;
      haveCounter_ = true;
    }
    int16_t raw[3] = {axis(word.data), axis(word.data + 2), axis(word.data + 4)};
    if (word.tag == LSM6DSV_XL_NC_TAG) {
#ifdef LEAF_IMU_STARTUP_TRACE
      if (!(traceSeen_ & 1)) {
        traceSeen_ |= 1;
        startupEvent("first_acceleration_record", traceStarted_);
      }
#endif
      // A clipped measurement is not a failed sensor. Reject its whole time slot without
      // restarting fusion or spending a hardware-recovery attempt.
      if (abs(int(raw[0])) >= 32760 || abs(int(raw[1])) >= 32760 || abs(int(raw[2])) >= 32760) {
        ++invalidAccel_;
        clippedSincePublish_ = true;
        pendingClipped_ = true;
        if (invalidAccel_ == 1 || uint32_t(now - lastClipReport_) >= 1000) {
          lastClipReport_ = now;
          Serial.printf("LSM6DSV: t=%lu clipped sample skipped raw=%d,%d,%d count=%lu\n",
                        (unsigned long)now, raw[0], raw[1], raw[2], (unsigned long)invalidAccel_);
        }
      }
      pending_.ax = lsm6dsv_from_fs8_to_mg(raw[0]) * .001;
      pending_.ay = lsm6dsv_from_fs8_to_mg(raw[1]) * .001;
      pending_.az = lsm6dsv_from_fs8_to_mg(raw[2]) * .001;
      pending_.hasAcceleration = true;
    } else {
#ifdef LEAF_IMU_STARTUP_TRACE
      if (!(traceSeen_ & 2)) {
        traceSeen_ |= 2;
        startupEvent("first_gravity_record", traceStarted_);
      }
#endif
      pending_.gx = lsm6dsv_from_sflp_to_mg(raw[0]) * .001;
      pending_.gy = lsm6dsv_from_sflp_to_mg(raw[1]) * .001;
      pending_.gz = lsm6dsv_from_sflp_to_mg(raw[2]) * .001;
      double norm2 =
          pending_.gx * pending_.gx + pending_.gy * pending_.gy + pending_.gz * pending_.gz;
      pending_.hasGravity = norm2 > .81 && norm2 < 1.21;
      if (!pending_.hasGravity) ++invalidGravity_;
    }
    if (pending_.hasAcceleration && pending_.hasGravity) {
      if (!pendingClipped_) {
        ++matched_;
        latest = pending_;
        // Publish only the newest complete pair once per host poll (~20 Hz). Host receive time
        // is used deliberately, never replaying buffered samples against a single baro reading.
        latest.t = now;
      }
      pending_ = MotionUpdate(now);
      pendingClipped_ = false;
      // Complete but clipped slots still prove acquisition is alive. Sustained saturation
      // expires instrument freshness normally; it must not reboot an otherwise healthy chip.
      lastPair_ = now;
    }
  }
  if (uint32_t(now - lastPair_) > (uint32_t(now - started_) < WARMUP_MS ? WARMUP_MS : 500)) {
    fault("No paired gravity data");
    return;
  }
#ifdef LEAF_IMU_STARTUP_TRACE
  const bool publish = latest.hasGravity && uint32_t(now - started_) >= WARMUP_MS && bus_;
#endif
  if (latest.hasGravity && uint32_t(now - started_) >= WARMUP_MS) {
    status_ = calibrated_ ? "Ready" : "Ready / no bias";
    if (bus_) {
      latest.clippedSinceLastUpdate = clippedSincePublish_;
      bus_->receive(latest);
      clippedSincePublish_ = false;
      ++published_;
      // Bound consecutive failed recoveries, not faults accumulated over an entire flight.
      if (retries_ && uint32_t(now - started_) >= 10000 && imu.velocityValid()) {
        retries_ = 0;
        Serial.println("LSM6DSV: recovery budget restored after healthy operation");
      }
      if (imu.velocityValid()) {
        const double climb = imu.kalmanVelocity();
        climbSum_ += climb;
        climbSumSquares_ += climb * climb;
        ++climbSamples_;
      }
    }
    if (uint32_t(now - lastReport_) >= 5000) {
      lastReport_ = now;
      Serial.printf(
          "LSM6DSV: a=%.4f,%.4f,%.4f g=%.4f,%.4f,%.4f pairs=%lu miss=%lu ready=%d vz=%.3f "
          "bias=%.4f learn=%d clips=%lu resets=%lu retries=%u\n",
          latest.ax, latest.ay, latest.az, latest.gx, latest.gy, latest.gz, (unsigned long)matched_,
          (unsigned long)mismatched_, imu.velocityValid(), imu.kalmanVelocity(),
          imu.accelerationBias(), imu.biasLearningAllowed(), (unsigned long)invalidAccel_,
          (unsigned long)resets_, retries_);
      if (climbSamples_) {
        Serial.printf("LSM6DSV climb: t=%lu n=%u mean=%.5f rms=%.5f\n", (unsigned long)now,
                      climbSamples_, climbSum_ / climbSamples_,
                      sqrt(climbSumSquares_ / climbSamples_));
      }
      climbSum_ = climbSumSquares_ = 0;
      climbSamples_ = 0;
    }
  }
#ifdef LEAF_IMU_STARTUP_TRACE
  if (latest.hasGravity) traceStartup(latest, fifo.fifo_level, publish);
#endif
}

#ifdef LEAF_IMU_STARTUP_TRACE
void LSM6DSV::traceStartup(const MotionUpdate& sample, uint16_t fifoWords, bool published) {
  if (!(traceSeen_ & 4)) {
    traceSeen_ |= 4;
    startupEvent("first_complete_pair", traceStarted_);
  }
  if (published && !(traceSeen_ & 8)) {
    traceSeen_ |= 8;
    startupEvent("first_published_pair", traceStarted_);
  }
  if (imu.velocityValid() && !(traceSeen_ & 16)) {
    traceSeen_ |= 16;
    startupEvent("first_valid_climb", traceStarted_);
  }
  // Observe startup and ongoing motion without feeding discarded pairs to the filter.
  // Avoid sustained debug-output backpressure when the USB capture host is absent.
  if (!Serial) return;
  double vertical = NAN, gravity = NAN;
  projectVerticalAcceleration(sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz,
                              vertical, gravity);
  const bool baroReady = baro.state() == Barometer::State::Ready;
  Serial.printf(
      "IMU_STARTUP_SAMPLE,%lu,%lu,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.6f,%ld,%lu,%d,%d,%d,%d,%u,"
      "%.5f,%.5f,%u,%d,%d\n",
      (unsigned long)sample.t, (unsigned long)(sample.t - traceStarted_), sample.ax, sample.ay,
      sample.az, sample.gx, sample.gy, sample.gz, vertical, baroReady ? (long)baro.pressure() : 0L,
      (unsigned long)baro.lastPressureTime(), baroReady, published, imu.kalmanValid(),
      imu.velocityValid(), imu.startupSamplesCompleted(), imu.kalmanVelocity(),
      imu.accelerationBias(), fifoWords, sample.clippedSinceLastUpdate, imu.biasLearningAllowed());
}
#endif

bool LSM6DSV::beginTestPhase(uint8_t phase) {
  testPhase_ = phase;
  testSamples_ = 0;
  discardTestSample_ = true;
  phaseStarted_ = millis();
  for (int i = 0; i < 3; ++i) sum_[i] = sumSq_[i] = accelSum_[i] = accelSumSq_[i] = 0;
  if (phase == 1) return lsm6dsv_xl_self_test_set(&ctx_, LSM6DSV_XL_ST_NEGATIVE) == 0;
  if (phase == 2)
    return lsm6dsv_xl_self_test_set(&ctx_, LSM6DSV_XL_ST_DISABLE) == 0 &&
           lsm6dsv_xl_data_rate_set(&ctx_, LSM6DSV_ODR_OFF) == 0 &&
           lsm6dsv_gy_data_rate_set(&ctx_, LSM6DSV_ODR_AT_240Hz) == 0;
  if (phase == 3) return lsm6dsv_gy_self_test_set(&ctx_, LSM6DSV_GY_ST_POSITIVE) == 0;
  if (phase == 4)
    return lsm6dsv_gy_self_test_set(&ctx_, LSM6DSV_GY_ST_DISABLE) == 0 &&
           lsm6dsv_gy_data_rate_set(&ctx_, LSM6DSV_ODR_AT_60Hz) == 0 &&
           lsm6dsv_xl_data_rate_set(&ctx_, LSM6DSV_ODR_AT_60Hz) == 0;
  return true;
}

void LSM6DSV::startSelfTest() {
  invalidate();
  running_ = false;
  testStarted_ = millis();
  testResult_ = TestResult::Running;
  status_ = "Self-test: keep still";
  selfTestInfo(
      "`Test=IMU`, `Chip=LSM6DSV`, `Address=0x6A`, `Message=Keep stationary for self-test and gyro "
      "bias`");
  uint8_t id = 0;
  if (sleeping_ || lsm6dsv_device_id_get(&ctx_, &id) != 0 || id != LSM6DSV_ID || !powerDown() ||
      lsm6dsv_sw_por(&ctx_) != 0 || lsm6dsv_block_data_update_set(&ctx_, 1) != 0 ||
      lsm6dsv_xl_full_scale_set(&ctx_, LSM6DSV_4g) != 0 ||
      lsm6dsv_gy_full_scale_set(&ctx_, LSM6DSV_2000dps) != 0 ||
      lsm6dsv_xl_data_rate_set(&ctx_, LSM6DSV_ODR_AT_60Hz) != 0 || !beginTestPhase(0))
    finishSelfTest(false);
}

void LSM6DSV::updateSelfTest() {
  if (uint32_t(millis() - testStarted_) > 15000) {
    finishSelfTest(false);
    return;
  }
  if (uint32_t(millis() - phaseStarted_) < 200) return;
  lsm6dsv_data_ready_t ready{};
  int16_t raw[3]{}, acc[3]{};
  if (lsm6dsv_flag_data_ready_get(&ctx_, &ready) != 0) {
    finishSelfTest(false);
    return;
  }
  bool gyro = testPhase_ >= 2;
  if ((!gyro && !ready.drdy_xl) || (gyro && !ready.drdy_gy) || (testPhase_ == 4 && !ready.drdy_xl))
    return;
  int result =
      gyro ? lsm6dsv_angular_rate_raw_get(&ctx_, raw) : lsm6dsv_acceleration_raw_get(&ctx_, raw);
  if (testPhase_ == 4) result |= lsm6dsv_acceleration_raw_get(&ctx_, acc);
  if (result != 0) {
    finishSelfTest(false);
    return;
  }
  if (discardTestSample_) {
    discardTestSample_ = false;
    return;
  }
  for (int i = 0; i < 3; ++i) {
    double v = gyro ? lsm6dsv_from_fs2000_to_mdps(raw[i]) * .001 : lsm6dsv_from_fs4_to_mg(raw[i]);
    sum_[i] += v;
    sumSq_[i] += v * v;
    double a = lsm6dsv_from_fs4_to_mg(acc[i]) * .001;
    accelSum_[i] += a;
    accelSumSq_[i] += a * a;
  }
  if (++testSamples_ < (testPhase_ == 4 ? 100 : 5)) return;
  bool pass = true;
  if (testPhase_ == 4) {
    BiasRecord bias{BIAS_VERSION, float(sum_[0] / 100), float(sum_[1] / 100), float(sum_[2] / 100)};
    pass = validBias(bias);
    double norm2 = 0;
    for (int i = 0; i < 3; ++i) {
      double mean = sum_[i] / 100, a = accelSum_[i] / 100;
      // Stationarity is required only here, during operator-controlled commissioning.
      pass &= sumSq_[i] / 100 - mean * mean < .04 && accelSumSq_[i] / 100 - a * a < .0004;
      norm2 += a * a;
    }
    pass &= norm2 > .81 && norm2 < 1.21;
    selfTestInfo("`Test=IMU`, `GyroBias_dps=%g,%g,%g`, `Stationary=%d`", bias.x, bias.y, bias.z,
                 pass);
    if (pass) {
      Preferences prefs;
      pass = prefs.begin("imu328", false);
      if (pass) {
        pass = prefs.putBytes("gyroBias", &bias, sizeof(bias)) == sizeof(bias);
        prefs.end();
      }
    }
    finishSelfTest(pass);
    return;
  }
  for (int i = 0; i < 3; ++i) {
    double mean = sum_[i] / 5;
    if ((testPhase_ & 1) == 0)
      baseline_[i] = mean;
    else {
      double response = fabs(mean - baseline_[i]);
      bool axisPass = response >= (gyro ? 150 : 50) && response <= (gyro ? 700 : 1700);
      pass &= axisPass;
      selfTestInfo("`Test=IMU`, `Sensor=%s`, `Axis=%d`, `Response=%g`, `Units=%s`, `Pass=%d`",
                   gyro ? "gyro" : "accel", i, response, gyro ? "dps" : "mg", axisPass);
    }
  }
  if (!pass || !beginTestPhase(testPhase_ + 1)) finishSelfTest(false);
}

void LSM6DSV::finishSelfTest(bool passed) {
  bool restored = powerDown();
  if (!sleeping_) {
    running_ = configure();
    restored &= running_;
  }
  testResult_ = passed && restored ? TestResult::Pass : TestResult::Fail;
  if (!restored) fault("Self-test restore fault");
  selfTestInfo("`Test=IMU`, `ElectricalAndBias=%s`, `ConfigRestored=%d`, `I2CErrors=%lu`",
               testResult_ == TestResult::Pass ? "PASS" : "FAIL", restored,
               (unsigned long)ioErrors_);
}

void LSM6DSV::cancelSelfTest() {
  if (testResult_ == TestResult::Running) finishSelfTest(false);
  testResult_ = TestResult::Idle;
}
#endif
