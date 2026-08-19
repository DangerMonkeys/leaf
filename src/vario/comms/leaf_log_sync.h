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
    Finishing,
    HostOwned,
    Backoff,
  };

  void update();
  void requestCancel();
  bool interceptsChargingButtons() const;
  bool canSleepWhileCharging() const;
  bool screenActive() const;
  bool retryPending() const { return state_ == State::Backoff; }
  bool progressKnown() const { return sessionTotalKnown_; }
  const char* statusLine() const;
  uint16_t currentCount() const {
    return completedCount_ < sessionTotalCount_ ? completedCount_ + 1 : sessionTotalCount_;
  }
  uint16_t totalCount() const { return sessionTotalCount_; }

 private:
  void beginSession(bool fromEject);
  void beginEligibilityScan();
  void finishToMassStorage();
  void handleTransientFailure();

  State state_ = State::Idle;
  File scanDirectory_;
  LeafLogCandidate current_;
  std::atomic<bool> cancelRequested_{false};
  bool wifiAttemptStarted_ = false;
  bool timeStarted_ = false;
  bool resumedAfterEject_ = false;
  bool sessionTotalKnown_ = false;
  uint16_t completedCount_ = 0;
  uint16_t sessionTotalCount_ = 0;
  uint16_t pendingCount_ = 0;
  uint32_t stateStartedMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint8_t retryIndex_ = 0;
};

extern LeafLogSync leafLogSync;
