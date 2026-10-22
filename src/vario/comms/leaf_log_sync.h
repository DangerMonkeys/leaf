#pragma once

#include <Arduino.h>
#include <FS.h>
#include <atomic>

#include "comms/leaf_log_client.h"
#include "logbook/logbook_store.h"

class LeafLogSync {
 public:
  static constexpr uint16_t MAX_SESSION_FLIGHTS = 100;
  enum class ScreenPhase : uint8_t { Scanning, Wifi, Connecting, Transfer, Paused, Complete };
  enum class State : uint8_t {
    Idle,
    CheckingEligibility,
    ConnectingWifi,
    WaitingForTime,
    Uploading,
    AwaitingCenterIntent,
    Finishing,
    Complete,
    HostOwned,
    Ejected,
    MassStorageUnavailable,
    Backoff,
  };

  void update();
  void prepareForCharging();
  void prepareForOperating();
  void requestCancel();
  void requestChargingClick();
  void requestPowerOn();
  bool takePowerOnReady();
  bool interceptsChargingButtons() const;
  bool canSleepWhileCharging() const;
  bool screenActive() const;
  ScreenPhase screenPhase() const;
  bool cancelConfirmation() const;
  bool transferInFlight() const { return uploadRunning_; }
  bool cancelPending() const { return cancelRequested_.load(std::memory_order_acquire); }
  bool retryPending() const { return state_ == State::Backoff; }
  bool massStorageUnavailable() const { return state_ == State::MassStorageUnavailable; }
  bool uploading() const { return state_ == State::Uploading; }
  bool scanning() const { return state_ == State::CheckingEligibility; }
  bool preparingNext() const {
    return state_ == State::CheckingEligibility && sessionTotalKnown_ && completedCount_ > 0 &&
           completedCount_ < sessionTotalCount_;
  }
  bool completionPending() const { return sessionCompletionPending_; }
  bool completionNotice() const { return state_ == State::Complete; }
  bool powerOnPending() const { return powerOnRequested_.load(std::memory_order_acquire); }
  bool powerOnClosingUsb() const { return powerOnPending() && powerOnClosingUsb_; }
  bool progressKnown() const { return sessionTotalKnown_; }
  const char* statusLine() const;
  uint16_t currentCount() const {
    return completedCount_ < sessionTotalCount_ ? completedCount_ + 1 : sessionTotalCount_;
  }
  uint16_t totalCount() const { return sessionTotalCount_; }

 private:
  void beginSession(bool fromEject);
  void beginEligibilityScan();
  void scanEligibility();
  void selectNextUpload();
  void startCurrentUpload();
  void clearSessionFiles();
  void finishRequestedExit();
  void finishForPowerOn();
  void finishToMassStorage();
  void recordCompletedItem();
  void handleTransientFailure(const char* reason, int httpStatus = 0, uint32_t elapsedMs = 0,
                              size_t fileSize = 0, size_t responseSize = 0,
                              const String& transportDetail = String());
  void launchUpload();
  void finishUpload(const leaf_log_client::UploadResult& result);
  static void uploadTask(void* context);

  State state_ = State::Idle;
  File scanDirectory_;
  File scanPaths_;
  File pendingPaths_;
  LeafLogCandidate current_;
  std::atomic<bool> cancelRequested_{false};
  std::atomic<bool> powerOnRequested_{false};
  std::atomic<bool> powerOnReady_{false};
  // The worker owns only its immutable request and result. The main loop keeps
  // all sync state, logbook writes and SD/USB ownership changes.
  bool uploadRunning_ = false;
  std::atomic<bool> uploadDone_{false};
  String uploadPath_;
  String uploadFilename_;
  String uploadToken_;
  leaf_log_client::UploadResult uploadResult_;
  bool cancelConfirmation_ = false;
  uint32_t cancelConfirmationStartedMs_ = 0;
  bool wifiAttemptStarted_ = false;
  bool timeStarted_ = false;
  bool resumedAfterEject_ = false;
  bool powerOnClosingUsb_ = false;
  bool sessionTotalKnown_ = false;
  bool sessionCompletionPending_ = false;
  uint16_t completedCount_ = 0;
  uint16_t sessionTotalCount_ = 0;
  uint16_t pendingCount_ = 0;
  uint32_t stateStartedMs_ = 0;
  uint32_t completionStartedMs_ = 0;
  uint32_t centerIntentStartedMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint8_t retryIndex_ = 0;
  State completionExitState_ = State::HostOwned;
};

extern LeafLogSync leafLogSync;
