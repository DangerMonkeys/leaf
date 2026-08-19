#include "comms/leaf_log_sync.h"

#include <SD_MMC.h>
#include <WiFi.h>
#include <time.h>

#include "comms/leaf_log_client.h"
#include "comms/leaf_log_credentials.h"
#include "comms/wifi_coordinator.h"
#include "storage/sd_card.h"
#include "system/usb_state.h"
#include "ui/settings/settings.h"

namespace {
  constexpr time_t VALID_TIME_EPOCH = 1704067200;
  constexpr uint32_t RETRY_DELAYS_MS[] = {5UL * 60 * 1000, 15UL * 60 * 1000, 60UL * 60 * 1000};
}  // namespace

LeafLogSync leafLogSync;

void LeafLogSync::beginEligibilityScan(bool fromEject) {
  if (scanDirectory_) scanDirectory_.close();
  current_ = LeafLogCandidate();
  pendingCount_ = 0;
  cancelRequested_.store(false, std::memory_order_release);
  wifiAttemptStarted_ = false;
  timeStarted_ = false;
  resumedAfterEject_ = fromEject;
  scanDirectory_ = SD_MMC.open(LogbookStore::directoryPath());
  state_ = State::CheckingEligibility;
  stateStartedMs_ = millis();
}

void LeafLogSync::update() {
  if (state_ == State::Idle) {
    if (sdcard.takeExplicitEject())
      beginEligibilityScan(true);
    else if (sdcard.ownership() == SDCardOwnership::FirmwareReserved)
      beginEligibilityScan(false);
    else
      state_ = State::HostOwned;
    return;
  }

  if (state_ == State::HostOwned) {
    if (sdcard.takeExplicitEject()) beginEligibilityScan(true);
    return;
  }

  if (cancelRequested_.load(std::memory_order_acquire) && state_ != State::Uploading) {
    finishToMassStorage();
    return;
  }

  switch (state_) {
    case State::CheckingEligibility: {
      const auto credential = leaf_log_credentials::load();
      if (!settings.labs_leafLog || !credential.linked() || !sdcard.isMounted() ||
          !scanDirectory_) {
        finishToMassStorage();
        return;
      }

      String path;
      while (!(path = scanDirectory_.getNextFileName()).isEmpty()) {
        path = LogbookStore::normalizePath(path);
        if (!LogbookStore::isLogbookJsonPath(path)) continue;
        LeafLogCandidate candidate;
        if (!LogbookStore::classifyForLeafLog(path, candidate)) return;
        if (candidate.disposition == LeafLogCandidate::Disposition::Rejected &&
            !candidate.rejectionReason.isEmpty() && !candidate.rejectionPersisted) {
          if (!LogbookStore::recordLeafLogRejection(path, candidate.rejectionReason)) {
            handleTransientFailure();
            return;
          }
          beginEligibilityScan(resumedAfterEject_);
          return;
        }
        if (candidate.disposition == LeafLogCandidate::Disposition::Pending) {
          pendingCount_++;
          if (current_.logbookPath.isEmpty()) current_ = candidate;
        }
        return;
      }
      scanDirectory_.close();
      if (current_.logbookPath.isEmpty()) {
        finishToMassStorage();
      } else if (!sdcard.reserveForFirmwareUpload() &&
                 sdcard.ownership() != SDCardOwnership::FirmwareUploading) {
        finishToMassStorage();
      } else {
        state_ = State::ConnectingWifi;
        stateStartedMs_ = millis();
      }
      break;
    }
    case State::ConnectingWifi:
      if (!wifiAttemptStarted_) {
        leaf_wifi::attemptSavedNetworkConnection();
        wifiAttemptStarted_ = true;
      }
      if (WiFi.status() == WL_CONNECTED) {
        state_ = State::WaitingForTime;
        stateStartedMs_ = millis();
      } else if (!leaf_wifi::savedNetworkConnectionInProgress()) {
        handleTransientFailure();
      }
      break;
    case State::WaitingForTime:
      if (time(nullptr) >= VALID_TIME_EPOCH) {
        state_ = State::Uploading;
      } else {
        if (!timeStarted_) {
          configTime(0, 0, "pool.ntp.org", "time.nist.gov");
          timeStarted_ = true;
        }
        if (millis() - stateStartedMs_ >= 8000) handleTransientFailure();
      }
      break;
    case State::Uploading: {
      const auto credential = leaf_log_credentials::load();
      auto result = leaf_log_client::uploadIgc(current_.trackPath, current_.trackFilename,
                                               credential.token, cancelRequested_);
      if (result.outcome == leaf_log_client::UploadOutcome::Delivered) {
        if (!LogbookStore::recordLeafLogFlightId(current_.logbookPath, result.flightId)) {
          handleTransientFailure();
          break;
        }
        if (!result.accountHandle.isEmpty() && !result.accountDisplayName.isEmpty()) {
          leaf_log_credentials::updateAccount(result.accountHandle, result.accountDisplayName);
        }
        uploadedCount_++;
        retryIndex_ = 0;
        beginEligibilityScan(resumedAfterEject_);
      } else if (result.outcome == leaf_log_client::UploadOutcome::Unauthorized) {
        leaf_log_credentials::markReconnectRequired();
        finishToMassStorage();
      } else if (result.outcome == leaf_log_client::UploadOutcome::Invalid ||
                 result.outcome == leaf_log_client::UploadOutcome::TooLarge) {
        LogbookStore::recordLeafLogRejection(
            current_.logbookPath, result.outcome == leaf_log_client::UploadOutcome::Invalid
                                      ? "invalid_igc"
                                      : "too_large");
        beginEligibilityScan(resumedAfterEject_);
      } else if (result.outcome == leaf_log_client::UploadOutcome::Cancelled) {
        finishToMassStorage();
      } else {
        handleTransientFailure();
      }
      break;
    }
    case State::Backoff:
      if (leaf_usb::hostMounted())
        finishToMassStorage();
      else if (static_cast<int32_t>(millis() - retryAtMs_) >= 0)
        beginEligibilityScan(false);
      break;
    case State::Finishing:
      finishToMassStorage();
      break;
    case State::Idle:
    case State::HostOwned:
      break;
  }
}

void LeafLogSync::finishToMassStorage() {
  if (scanDirectory_) scanDirectory_.close();
  leaf_wifi::disconnectFromNetwork();
  cancelRequested_.store(false, std::memory_order_release);
  if (resumedAfterEject_)
    sdcard.keepMassStorageEjected();
  else
    sdcard.presentMassStorage();
  state_ = State::HostOwned;
}

void LeafLogSync::handleTransientFailure() {
  if (leaf_usb::hostMounted()) {
    finishToMassStorage();
    return;
  }
  leaf_wifi::disconnectFromNetwork();
  const uint8_t index = min<uint8_t>(retryIndex_, 2);
  retryAtMs_ = millis() + RETRY_DELAYS_MS[index];
  if (retryIndex_ < 2) retryIndex_++;
  state_ = State::Backoff;
}

void LeafLogSync::requestCancel() { cancelRequested_.store(true, std::memory_order_release); }

bool LeafLogSync::interceptsChargingButtons() const {
  return state_ == State::CheckingEligibility || state_ == State::ConnectingWifi ||
         state_ == State::WaitingForTime || state_ == State::Uploading;
}

bool LeafLogSync::canSleepWhileCharging() const {
  return state_ == State::Idle || state_ == State::HostOwned || state_ == State::Backoff;
}

bool LeafLogSync::screenActive() const { return interceptsChargingButtons(); }

const char* LeafLogSync::statusLine() const {
  switch (state_) {
    case State::CheckingEligibility:
      return "Checking Leaf Log...";
    case State::ConnectingWifi:
      return "Connecting...";
    case State::WaitingForTime:
      return "Setting network time...";
    case State::Uploading:
      return "Uploading to Leaf Log";
    default:
      return "";
  }
}
