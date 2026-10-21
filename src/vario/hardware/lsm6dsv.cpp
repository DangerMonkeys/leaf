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
  // Four electrical phases, up to three five-second bias windows, then flat capture.
  // Leave time within the enclosing 30-second IMU test for valid climb to return.
  constexpr uint32_t SELF_TEST_TIMEOUT_MS = 25000;
  static_assert(SFLP_ACCEL_FULL_SCALE_G == 8.0,
                "Update the operating full scale and FIFO conversion together");
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
  invalidate();
  uint8_t id = 0;
  if (lsm6dsv_device_id_get(&ctx_, &id) != 0 || id != LSM6DSV_ID) {
    Serial.printf("LSM6DSV: WHO_AM_I=0x%02X, expected 0x%02X at 0x6A\n", id, LSM6DSV_ID);
    return false;
  }
  // POR also clears all interrupt routes and the sensor hub. No magnetometer access.
  if (lsm6dsv_sw_por(&ctx_) != 0) return false;
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

  Preferences prefs;
  BiasRecord bias{};
  calibrated_ = false;
  if (prefs.begin("imu328", true)) {
    calibrated_ = prefs.getBytesLength("gyroBias") == sizeof(bias) &&
                  prefs.getBytes("gyroBias", &bias, sizeof(bias)) == sizeof(bias) &&
                  validBias(bias);
    prefs.end();
  }
  FlatAccelerationBias flatBias{};
  if (prefs.begin("imu328", true)) {
    if (prefs.getBytesLength("flatBias") != sizeof(flatBias) ||
        prefs.getBytes("flatBias", &flatBias, sizeof(flatBias)) != sizeof(flatBias) ||
        !flatBias.valid())
      flatBias = {};
    prefs.end();
  }
  imu.setAccelerationBiasReference(flatBias);
  // Seed even an uncommissioned board with zero rather than learning a gyro bias in flight.
  lsm6dsv_sflp_gbias_t seed{};
  if (calibrated_) {
    seed.gbias_x = bias.x;
    seed.gbias_y = bias.y;
    seed.gbias_z = bias.z;
  }
  if (lsm6dsv_sflp_game_gbias_set(&ctx_, &seed) != 0) return false;
  lsm6dsv_fifo_sflp_raw_t batch{};
  batch.gravity = 1;
  if (lsm6dsv_fifo_sflp_batch_set(&ctx_, batch) != 0 ||
      lsm6dsv_fifo_xl_batch_set(&ctx_, LSM6DSV_XL_BATCHED_AT_60Hz) != 0 ||
      lsm6dsv_fifo_mode_set(&ctx_, LSM6DSV_STREAM_MODE) != 0)
    return false;
  started_ = lastPair_ = lastPoll_ = millis();
  status_ = "Warming up";
  Serial.printf("LSM6DSV: id=0x%02X, 60Hz, +/-8g, SFLP gravity, gyro bias %s\n", id,
                calibrated_ ? "restored" : "NOT commissioned (zero seed)");
  Serial.printf("LSM6DSV: flat bias %s %.6f g\n", flatBias.valid() ? "restored" : "absent",
                flatBias.residualG);
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
  if (testResult_ == TestResult::Running) {
    testResult_ = TestResult::Fail;
    selfTestInfo("`Test=IMU`, `Result=FAIL`, `Message=%s`", reason);
  }
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
    if (testPhase_ < 5) {
      updateSelfTest();
      return;
    }
    if (uint32_t(millis() - testStarted_) > SELF_TEST_TIMEOUT_MS) {
      finishSelfTest(false);
      return;
    }
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
        if (testResult_ == TestResult::Running && testPhase_ == 5 &&
            uint32_t(now - started_) >= 1000 && testSamples_ < 100)
          captureFlatBias(pending_);
        ++matched_;
        latest = pending_;
        // Publish only the newest complete pair once per host poll (~20 Hz). Host receive time
        // is used deliberately, never replaying buffered samples against a single baro reading.
        latest.t = now;
      } else if (testResult_ == TestResult::Running && testPhase_ == 5) {
        flatCaptureFailed_ = true;
      }
      pending_ = MotionUpdate(now);
      pendingClipped_ = false;
      // Complete but clipped slots still prove acquisition is alive. Sustained saturation
      // expires instrument freshness normally; it must not reboot an otherwise healthy chip.
      lastPair_ = now;
    }
  }
  if (testResult_ == TestResult::Running && testPhase_ == 5 && testSamples_ >= 100) {
    finishFlatBiasCapture();
    return;
  }
  if (uint32_t(now - lastPair_) > (uint32_t(now - started_) < WARMUP_MS ? WARMUP_MS : 500)) {
    fault("No paired gravity data");
    return;
  }
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
}

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
  biasWindows_ = 0;
  testResult_ = TestResult::Running;
  status_ = "Self-test: keep still";
  Serial.println("LSM6DSV: keep stationary for self-test and gyro bias");
  uint8_t id = 0;
  if (sleeping_ || lsm6dsv_device_id_get(&ctx_, &id) != 0 || id != LSM6DSV_ID || !powerDown() ||
      lsm6dsv_sw_por(&ctx_) != 0 || lsm6dsv_block_data_update_set(&ctx_, 1) != 0 ||
      lsm6dsv_xl_full_scale_set(&ctx_, LSM6DSV_4g) != 0 ||
      lsm6dsv_gy_full_scale_set(&ctx_, LSM6DSV_2000dps) != 0 ||
      lsm6dsv_xl_data_rate_set(&ctx_, LSM6DSV_ODR_AT_60Hz) != 0 || !beginTestPhase(0))
    finishSelfTest(false);
}

void LSM6DSV::updateSelfTest() {
  if (uint32_t(millis() - testStarted_) > SELF_TEST_TIMEOUT_MS) {
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
    double gyroStd[3]{}, accelStd[3]{};
    for (int i = 0; i < 3; ++i) {
      double mean = sum_[i] / 100, a = accelSum_[i] / 100;
      // Stationarity is required only here, during operator-controlled commissioning.
      pass &= sumSq_[i] / 100 - mean * mean < .04 && accelSumSq_[i] / 100 - a * a < .0004;
      gyroStd[i] = sqrt(fmax(0, sumSq_[i] / 100 - mean * mean));
      accelStd[i] = sqrt(fmax(0, accelSumSq_[i] / 100 - a * a));
      norm2 += a * a;
    }
    pass &= norm2 > .81 && norm2 < 1.21;
    const bool stationary = pass;
    ++biasWindows_;
    if (!stationary)
      selfTestInfo(
          "`Test=IMU`, `Message=Stationarity limit exceeded`, `BiasValid=%d`, "
          "`MeanAccel_g=%.6f`, `GyroStd_dps=%.4f,%.4f,%.4f`, "
          "`AccelStd_g=%.6f,%.6f,%.6f`",
          validBias(bias), sqrt(norm2), gyroStd[0], gyroStd[1], gyroStd[2], accelStd[0],
          accelStd[1], accelStd[2]);
    // Desk taps can spoil one otherwise stationary window. Retry a fresh, complete
    // window without relaxing any limits or mixing rejected data into the saved bias.
    if (!stationary && biasWindows_ < 3) {
      selfTestInfo("`Test=IMU`, `Message=Retrying stationary measurement`, `Attempt=%u/3`",
                   biasWindows_ + 1);
      if (!beginTestPhase(4)) finishSelfTest(false);
      return;
    }
    if (pass) {
      Preferences prefs;
      pass = prefs.begin("imu328", false);
      if (pass) {
        pass = prefs.putBytes("gyroBias", &bias, sizeof(bias)) == sizeof(bias);
        prefs.end();
      }
    }
    selfTestInfo("`Test=IMU`, `GyroBias_dps=%.4f,%.4f,%.4f`, `Stationary=%d`, `Saved=%d`", bias.x,
                 bias.y, bias.z, stationary, pass);
    finishSelfTest(pass);
    return;
  }
  double response[3]{};
  for (int i = 0; i < 3; ++i) {
    double mean = sum_[i] / 5;
    if ((testPhase_ & 1) == 0)
      baseline_[i] = mean;
    else {
      response[i] = fabs(mean - baseline_[i]);
      bool axisPass = response[i] >= (gyro ? 150 : 50) && response[i] <= (gyro ? 700 : 1700);
      pass &= axisPass;
    }
  }
  if (testPhase_ & 1)
    selfTestInfo("`Test=IMU`, `Sensor=%s`, `Response_%s=%.3f,%.3f,%.3f`, `Pass=%d`",
                 gyro ? "gyro" : "accel", gyro ? "dps" : "mg", response[0], response[1],
                 response[2], pass);
  if (!pass || !beginTestPhase(testPhase_ + 1)) finishSelfTest(false);
}

void LSM6DSV::finishSelfTest(bool passed) {
  bool restored = powerDown();
  if (!sleeping_) {
    running_ = configure();
    restored &= running_;
  }
  testResult_ = passed && restored ? TestResult::Pass : TestResult::Fail;
  if (testResult_ == TestResult::Pass) {
    // Capture normal-range data during the existing three-second warm-up. Keep the same
    // operator stillness prompt and do not restart acquisition when capture finishes.
    beginTestPhase(5);
    flatCaptureFailed_ = false;
    residualSum_ = residualSumSq_ = 0;
    for (int i = 0; i < 3; ++i) gravitySum_[i] = gravitySumSq_[i] = 0;
    testResult_ = TestResult::Running;
  }
  if (!restored) fault("Self-test restore fault");
  // A clean restore is implicit in the final IMU result. Keep extra details for faults.
  if (!passed || !restored || ioErrors_)
    selfTestInfo("`Test=IMU`, `ElectricalAndBias=%s`, `ConfigRestored=%d`, `I2CErrors=%lu`",
                 passed && restored ? "PASS" : "FAIL", restored, (unsigned long)ioErrors_);
}

void LSM6DSV::captureFlatBias(const MotionUpdate& sample) {
  double residual = 0, gravity = 0;
  if (!projectVerticalAcceleration(sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz,
                                   residual, gravity)) {
    flatCaptureFailed_ = true;
    return;
  }
  const double a[3] = {sample.ax, sample.ay, sample.az};
  const double g[3] = {sample.gx / gravity, sample.gy / gravity, sample.gz / gravity};
  for (int i = 0; i < 3; ++i) {
    accelSum_[i] += a[i];
    accelSumSq_[i] += a[i] * a[i];
    gravitySum_[i] += g[i];
    gravitySumSq_[i] += g[i] * g[i];
  }
  residualSum_ += residual;
  residualSumSq_ += residual * residual;
  ++testSamples_;
}

void LSM6DSV::finishFlatBiasCapture() {
  FlatAccelerationBias reference{};
  reference.version = FlatAccelerationBias::VERSION;
  reference.residualG = residualSum_ / 100;
  const double norm = sqrt(gravitySum_[0] * gravitySum_[0] + gravitySum_[1] * gravitySum_[1] +
                           gravitySum_[2] * gravitySum_[2]);
  const double inverseNorm = norm > 1e-6 ? 1 / norm : 0;
  reference.gx = gravitySum_[0] * inverseNorm;
  reference.gy = gravitySum_[1] * inverseNorm;
  reference.gz = gravitySum_[2] * inverseNorm;
  bool pass = !flatCaptureFailed_ && reference.valid();
  double accelNorm2 = 0;
  for (int i = 0; i < 3; ++i) {
    const double a = accelSum_[i] / 100, g = gravitySum_[i] / 100;
    accelNorm2 += a * a;
    pass &= accelSumSq_[i] / 100 - a * a < .0004 && gravitySumSq_[i] / 100 - g * g < .0004;
  }
  pass &= accelNorm2 > .81 && accelNorm2 < 1.21 &&
          residualSumSq_ / 100 - reference.residualG * reference.residualG < .0004;
  if (pass) {
    Preferences prefs;
    pass = prefs.begin("imu328", false);
    if (pass) {
      FlatAccelerationBias restored{};
      pass = prefs.putBytes("flatBias", &reference, sizeof(reference)) == sizeof(reference) &&
             prefs.getBytes("flatBias", &restored, sizeof(restored)) == sizeof(restored) &&
             restored.valid() && restored.residualG == reference.residualG &&
             restored.gx == reference.gx && restored.gy == reference.gy &&
             restored.gz == reference.gz;
      prefs.end();
    }
  }
  selfTestInfo("`Test=IMU`, `FlatBias_g=%.6f`, `Saved=%d`", reference.residualG, pass);
  if (pass) {
    imu.setAccelerationBiasReference(reference);
    imu.wake();
    testResult_ = TestResult::Pass;
  } else {
    finishSelfTest(false);
  }
}

void LSM6DSV::cancelSelfTest() {
  if (testResult_ == TestResult::Running) finishSelfTest(false);
  testResult_ = TestResult::Idle;
}
#endif
