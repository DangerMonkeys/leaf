#pragma once

#include <lsm6dsv_reg.h>
#include "dispatch/message_source.h"
#include "dispatch/message_types.h"
#include "dispatch/pollable.h"

// Leaf v3.2.8: polling only. INT1 is wired through TCA9555 P14; INT2 is unused.
class LSM6DSV : public IPollable, public IMessageSource {
 public:
  static LSM6DSV& getInstance() {
    static LSM6DSV instance;
    return instance;
  }
  void init();
  void update();
  void sleep();
  void wake();
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

  enum class TestResult { Idle, Running, Pass, Fail };
  void startSelfTest();
  void cancelSelfTest();
  TestResult testResult() const { return testResult_; }
  const char* status() const { return status_; }
  bool calibrated() const { return calibrated_; }
  uint32_t ioErrorCount() const { return ioErrors_; }
  uint32_t motionFifoPacketCount() const { return packets_; }
  uint32_t motionMatchedPacketCount() const { return matched_; }
  uint32_t motionMismatchedPacketCount() const { return mismatched_; }
  uint32_t motionPublishedSampleCount() const { return published_; }
  uint32_t imuUpdateCallCount() const { return updates_; }
  uint32_t fifoNoDataCount() const { return empty_; }
  uint32_t invalidOrientationPacketCount() const { return invalidGravity_; }
  uint32_t invalidAccelerationPacketCount() const { return invalidAccel_; }
  uint32_t fifoResetCount() const { return resets_; }

 private:
  static int32_t read(void*, uint8_t, uint8_t*, uint16_t);
  static int32_t write(void*, uint8_t, const uint8_t*, uint16_t);
  bool configure();
  bool powerDown();
  void invalidate();
  void fault(const char* reason);
  void updateSelfTest();
  void finishSelfTest(bool passed);
  bool beginTestPhase(uint8_t phase);
  void captureFlatBias(const MotionUpdate& sample);
  void finishFlatBiasCapture();

  stmdev_ctx_t ctx_{};
  etl::imessage_bus* bus_ = nullptr;
  bool sleeping_ = true, running_ = false, calibrated_ = false;
  const char* status_ = "Off";
  uint32_t started_ = 0, lastPair_ = 0, lastPoll_ = 0, retryAt_ = 0, lastReport_ = 0;
  uint8_t retries_ = 0;
  MotionUpdate pending_{0};
  uint8_t counter_ = 0;
  bool haveCounter_ = false, pendingClipped_ = false;
  bool clippedSincePublish_ = false;
  uint32_t lastClipReport_ = 0;
  uint32_t packets_ = 0, matched_ = 0, mismatched_ = 0, published_ = 0, updates_ = 0;
  uint32_t empty_ = 0, invalidGravity_ = 0, invalidAccel_ = 0, resets_ = 0, ioErrors_ = 0;
  double climbSum_ = 0, climbSumSquares_ = 0;
  uint16_t climbSamples_ = 0;

  TestResult testResult_ = TestResult::Idle;
  uint8_t testPhase_ = 0, testSamples_ = 0, biasWindows_ = 0;
  uint32_t testStarted_ = 0, phaseStarted_ = 0;
  bool discardTestSample_ = true;
  bool flatCaptureFailed_ = false;
  double residualSum_ = 0, residualSumSq_ = 0;
  double gravitySum_[3]{}, gravitySumSq_[3]{};
  double sum_[3]{}, sumSq_[3]{}, baseline_[3]{}, accelSum_[3]{}, accelSumSq_[3]{};
};
