#pragma once

#include <Arduino.h>
#include <FS.h>
#include <atomic>

#include "logbook/logbook_store.h"

class LeafLogSync {
 public:
  enum class State : uint8_t {
    Idle,
    CheckingEligibility,
    ConnectingWifi,
    WaitingForTime,
    Uploading,
    AwaitingCenterIntent,
    Finishing,
    HostOwned,
    Ejected,
    MassStorageUnavailable,
    Backoff,
  };

  void update();
  void prepareForCharging();
  void prepareForOperating();
  void requestCancel();
  void requestPowerOn();
  bool takePowerOnReady();
  bool interceptsChargingButtons() const;
  bool canSleepWhileCharging() const;
  bool screenActive() const;
  bool retryPending() const { return state_ == State::Backoff; }
  bool massStorageUnavailable() const { return state_ == State::MassStorageUnavailable; }
  bool powerOnPending() const { return powerOnRequested_.load(std::memory_order_acquire); }
  bool progressKnown() const { return sessionTotalKnown_; }
  const char* statusLine() const;
  uint16_t currentCount() const {
    return completedCount_ < sessionTotalCount_ ? completedCount_ + 1 : sessionTotalCount_;
  }
  uint16_t totalCount() const { return sessionTotalCount_; }

 private:
  void beginSession(bool fromEject);
  void beginEligibilityScan();
  void finishRequestedExit();
  void finishForPowerOn();
  void finishToMassStorage();
  void handleTransientFailure(const char* reason, int httpStatus = 0, uint32_t elapsedMs = 0,
                              size_t fileSize = 0, size_t responseSize = 0);

  State state_ = State::Idle;
  File scanDirectory_;
  LeafLogCandidate current_;
  std::atomic<bool> cancelRequested_{false};
  std::atomic<bool> powerOnRequested_{false};
  std::atomic<bool> powerOnReady_{false};
  bool wifiAttemptStarted_ = false;
  bool timeStarted_ = false;
  bool resumedAfterEject_ = false;
  bool sessionTotalKnown_ = false;
  uint16_t completedCount_ = 0;
  uint16_t sessionTotalCount_ = 0;
  uint16_t pendingCount_ = 0;
  uint32_t stateStartedMs_ = 0;
  uint32_t centerIntentStartedMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint8_t retryIndex_ = 0;
};

extern LeafLogSync leafLogSync;
