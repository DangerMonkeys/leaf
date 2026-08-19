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
  const char* statusLine() const;
  uint16_t uploadedCount() const { return uploadedCount_; }
  uint16_t pendingCount() const { return pendingCount_; }

 private:
  void beginEligibilityScan(bool fromEject = false);
  void finishToMassStorage();
  void handleTransientFailure();

  State state_ = State::Idle;
  File scanDirectory_;
  LeafLogCandidate current_;
  std::atomic<bool> cancelRequested_{false};
  bool wifiAttemptStarted_ = false;
  bool timeStarted_ = false;
  bool resumedAfterEject_ = false;
  uint16_t uploadedCount_ = 0;
  uint16_t pendingCount_ = 0;
  uint32_t stateStartedMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint8_t retryIndex_ = 0;
};

extern LeafLogSync leafLogSync;
