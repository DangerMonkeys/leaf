#include "comms/leaf_log_sync.h"

#include <SD_MMC.h>
#include <WiFi.h>
#include <time.h>

#include "comms/leaf_log_client.h"
#include "comms/leaf_log_credentials.h"
#include "comms/wifi_coordinator.h"
#include "diagnostics/diagnostic_logs.h"
#include "diagnostics/diagnostic_network/diagnostic_network.h"
#include "hardware/buttons.h"
#include "storage/sd_card.h"
#include "system/usb_state.h"
#include "ui/settings/settings.h"

namespace {
  constexpr time_t VALID_TIME_EPOCH = 1704067200;
  constexpr uint32_t CENTER_POWER_ON_HOLD_MS = 900;
  constexpr uint32_t COMPLETION_POPUP_MS = 5UL * 1000;
  constexpr uint32_t NOTICE_POPUP_MS = 30UL * 1000;
  constexpr uint32_t RETRY_DELAYS_MS[] = {5UL * 60 * 1000, 15UL * 60 * 1000, 60UL * 60 * 1000};
}  // namespace

LeafLogSync leafLogSync;

void LeafLogSync::beginSession(bool fromEject) {
  buttons.armUrgentPressCapture();
  resumedAfterEject_ = fromEject;
  sessionTotalKnown_ = false;
  sessionCompletionPending_ = false;
  completedCount_ = 0;
  sessionTotalCount_ = 0;
  completionStartedMs_ = 0;
  retryIndex_ = 0;
  beginEligibilityScan();
}

void LeafLogSync::beginEligibilityScan() {
  if (scanDirectory_) scanDirectory_.close();
  current_ = LeafLogCandidate();
  pendingCount_ = 0;
  cancelRequested_.store(false, std::memory_order_release);
  wifiAttemptStarted_ = false;
  timeStarted_ = false;
  scanDirectory_ = SD_MMC.open(LogbookStore::directoryPath());
  state_ = State::CheckingEligibility;
  stateStartedMs_ = millis();
}

void LeafLogSync::update() {
  // Turning on always takes precedence over charging-mode work and commissioning suppression.
  // The user must never be trapped on the charging screen by an SD/Leaf Log policy decision.
  if (powerOnRequested_.load(std::memory_order_acquire)) {
    finishForPowerOn();
    return;
  }

  if (diagnostic_network.chargingWorkBlocked()) {
    // Give factory discovery first use of WiFi and the SD card. If no factory network is found,
    // ordinary Leaf Log upload and USB mass storage continue during this charging session.
    sdcard.keepMassStorageEjected();
    return;
  }

  if (state_ == State::Idle) {
    if (sdcard.takeExplicitEject())
      beginSession(true);
    else if (sdcard.isMounted() && sdcard.ownership() == SDCardOwnership::FirmwareReserved)
      beginSession(false);
    else
      state_ = State::HostOwned;
    return;
  }

  if (state_ == State::HostOwned) {
    if (sdcard.takeExplicitEject())
      beginSession(true);
    else if (sdcard.isMounted() && sdcard.ownership() == SDCardOwnership::FirmwareReserved)
      beginSession(false);
    return;
  }

  if (state_ == State::Ejected) {
    // Windows may send more than one eject command. Consume duplicates without starting another
    // Leaf Log session, and remain ejected until the card is physically removed or Leaf powers on.
    sdcard.takeExplicitEject();
    if (!SDCard::isCardPresent()) {
      resumedAfterEject_ = false;
      state_ = State::Idle;
    }
    return;
  }

  if (buttons.urgentPressLatched()) requestCancel();

  if (cancelRequested_.load(std::memory_order_acquire) && state_ != State::Uploading) {
    finishRequestedExit();
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
            handleTransientFailure("record_rejection_failed");
            return;
          }
          beginEligibilityScan();
          return;
        }
        if (candidate.disposition == LeafLogCandidate::Disposition::Pending) {
          pendingCount_++;
          if (current_.logbookPath.isEmpty()) current_ = candidate;
        }
        return;
      }
      scanDirectory_.close();
      if (!sessionTotalKnown_) {
        sessionTotalCount_ = pendingCount_;
        sessionTotalKnown_ = true;
      }
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
    case State::ConnectingWifi: {
      if (WiFi.status() == WL_CONNECTED && !diagnostic_network.connected()) {
        state_ = State::WaitingForTime;
        stateStartedMs_ = millis();
        break;
      }

      if (!wifiAttemptStarted_) {
        leaf_wifi::attemptSavedNetworkConnection();
        wifiAttemptStarted_ = true;
      }

      const bool connectionInProgress = leaf_wifi::savedNetworkConnectionInProgress();
      if (WiFi.status() == WL_CONNECTED && !diagnostic_network.connected()) {
        state_ = State::WaitingForTime;
        stateStartedMs_ = millis();
      } else if (!connectionInProgress) {
        handleTransientFailure("wifi_connect_failed", static_cast<int>(WiFi.status()),
                               millis() - stateStartedMs_);
      }
      break;
    }
    case State::WaitingForTime:
      if (time(nullptr) >= VALID_TIME_EPOCH) {
        state_ = State::Uploading;
      } else {
        if (!timeStarted_) {
          configTime(0, 0, "pool.ntp.org", "time.nist.gov");
          timeStarted_ = true;
        }
        if (millis() - stateStartedMs_ >= 8000) {
          handleTransientFailure("network_time_timeout", 0, millis() - stateStartedMs_);
        }
      }
      break;
    case State::Uploading: {
      const auto credential = leaf_log_credentials::load();
      auto result =
          leaf_log_client::uploadIgc(current_.trackPath, current_.trackFilename, credential.token,
                                     cancelRequested_, buttons.urgentPressSignal());
      if (result.outcome == leaf_log_client::UploadOutcome::Delivered) {
        if (!LogbookStore::recordLeafLogFlightId(current_.logbookPath, result.flightId)) {
          handleTransientFailure("record_flight_id_failed", result.httpStatus, result.elapsedMs,
                                 result.fileSize, result.responseSize);
          break;
        }
        if (!result.accountHandle.isEmpty() && !result.accountDisplayName.isEmpty()) {
          leaf_log_credentials::updateAccount(result.accountHandle, result.accountDisplayName);
        }
        recordCompletedItem();
        retryIndex_ = 0;
        beginEligibilityScan();
      } else if (result.outcome == leaf_log_client::UploadOutcome::Unauthorized) {
        leaf_log_credentials::markReconnectRequired();
        finishToMassStorage();
      } else if (result.outcome == leaf_log_client::UploadOutcome::Invalid ||
                 result.outcome == leaf_log_client::UploadOutcome::TooLarge) {
        if (!LogbookStore::recordLeafLogRejection(
                current_.logbookPath, result.outcome == leaf_log_client::UploadOutcome::Invalid
                                          ? "invalid_igc"
                                          : "too_large")) {
          handleTransientFailure("record_rejection_failed", result.httpStatus, result.elapsedMs,
                                 result.fileSize, result.responseSize);
          break;
        }
        recordCompletedItem();
        retryIndex_ = 0;
        beginEligibilityScan();
      } else if (result.outcome == leaf_log_client::UploadOutcome::Cancelled) {
        finishRequestedExit();
      } else {
        handleTransientFailure(result.diagnostic, result.httpStatus, result.elapsedMs,
                               result.fileSize, result.responseSize, result.transportDetail);
      }
      break;
    }
    case State::Backoff:
      if (static_cast<int32_t>(millis() - retryAtMs_) >= 0) beginEligibilityScan();
      break;
    case State::AwaitingCenterIntent:
      finishRequestedExit();
      break;
    case State::Finishing:
      finishToMassStorage();
      break;
    case State::Complete:
      if (millis() - stateStartedMs_ >= COMPLETION_POPUP_MS) {
        sessionCompletionPending_ = false;
        state_ = completionExitState_;
      }
      break;
    case State::MassStorageUnavailable:
    case State::Ejected:
    case State::Idle:
    case State::HostOwned:
      break;
  }
}

void LeafLogSync::prepareForCharging() {
  if (scanDirectory_) scanDirectory_.close();
  buttons.disarmUrgentPressCapture();
  cancelRequested_.store(false, std::memory_order_release);
  powerOnRequested_.store(false, std::memory_order_release);
  powerOnReady_.store(false, std::memory_order_release);
  centerIntentStartedMs_ = 0;
  resumedAfterEject_ = false;
  powerOnClosingUsb_ = false;
  sessionCompletionPending_ = false;
  completionStartedMs_ = 0;
  state_ = State::Idle;
}

void LeafLogSync::prepareForOperating() {
  if (scanDirectory_) scanDirectory_.close();
  buttons.disarmUrgentPressCapture();
  cancelRequested_.store(false, std::memory_order_release);
  powerOnRequested_.store(false, std::memory_order_release);
  centerIntentStartedMs_ = 0;
  resumedAfterEject_ = false;
  powerOnClosingUsb_ = false;
  sessionCompletionPending_ = false;
  completionStartedMs_ = 0;
  state_ = State::Idle;
}

void LeafLogSync::recordCompletedItem() {
  completedCount_++;
  if (sessionTotalKnown_ && sessionTotalCount_ > 0 && completedCount_ >= sessionTotalCount_) {
    sessionCompletionPending_ = true;
    completionStartedMs_ = millis();
  }
}

void LeafLogSync::finishRequestedExit() {
  if (powerOnRequested_.load(std::memory_order_acquire)) {
    finishForPowerOn();
    return;
  }

  if (buttons.inspectPins() == Button::CENTER) {
    if (centerIntentStartedMs_ == 0) centerIntentStartedMs_ = millis();
    if (millis() - centerIntentStartedMs_ >= CENTER_POWER_ON_HOLD_MS) {
      finishForPowerOn();
    } else {
      state_ = State::AwaitingCenterIntent;
    }
    return;
  }

  finishToMassStorage();
}

void LeafLogSync::finishForPowerOn() {
  buttons.disarmUrgentPressCapture();
  buttons.suppressEventsUntilRelease();
  if (scanDirectory_) scanDirectory_.close();
  leaf_wifi::disconnectFromNetwork();
  cancelRequested_.store(false, std::memory_order_release);
  centerIntentStartedMs_ = 0;
  resumedAfterEject_ = false;
  if (!sdcard.acquireForFirmwareUse(0, true)) {
    state_ = State::AwaitingCenterIntent;
    return;
  }
  powerOnRequested_.store(false, std::memory_order_release);
  state_ = State::Idle;
  powerOnReady_.store(true, std::memory_order_release);
}

void LeafLogSync::finishToMassStorage() {
  const bool suppressButton = buttons.urgentPressLatched();
  buttons.disarmUrgentPressCapture();
  if (suppressButton) buttons.suppressEventsUntilRelease();
  if (scanDirectory_) scanDirectory_.close();
  leaf_wifi::disconnectFromNetwork();
  cancelRequested_.store(false, std::memory_order_release);
  powerOnRequested_.store(false, std::memory_order_release);
  centerIntentStartedMs_ = 0;
  State exitState = State::HostOwned;
  if (resumedAfterEject_) {
    sdcard.keepMassStorageEjected();
    resumedAfterEject_ = false;
    exitState = State::Ejected;
  } else {
    if (!sdcard.presentMassStorage()) {
      state_ = State::MassStorageUnavailable;
      stateStartedMs_ = millis();
      return;
    }
  }

  if (sessionCompletionPending_) {
    completionExitState_ = exitState;
    state_ = State::Complete;
    stateStartedMs_ = millis();
  } else {
    state_ = exitState;
  }
}

void LeafLogSync::handleTransientFailure(const char* reason, int httpStatus, uint32_t elapsedMs,
                                         size_t fileSize, size_t responseSize,
                                         const String& transportDetail) {
  leaf_wifi::disconnectFromNetwork();
  const uint8_t index = min<uint8_t>(retryIndex_, 2);
  const uint32_t delayMs = RETRY_DELAYS_MS[index];
  retryAtMs_ = millis() + delayMs;
  if (retryIndex_ < 2) retryIndex_++;

  String detail = "reason=";
  detail += reason ? reason : "unknown";
  detail += ",track=";
  detail += current_.trackFilename;
  detail += ",http=";
  detail += httpStatus;
  detail += ",elapsed_ms=";
  detail += elapsedMs;
  detail += ",file_bytes=";
  detail += fileSize;
  detail += ",response_bytes=";
  detail += responseSize;
  if (!transportDetail.isEmpty()) {
    detail += ',';
    detail += transportDetail;
  }
  Serial.printf("Leaf Log retry: %s delay_ms=%lu\n", detail.c_str(),
                static_cast<unsigned long>(delayMs));
  diagnostic_logs::appendSystemEvent("leaf_log", "retry", detail, "delay_ms", delayMs, true);
  state_ = State::Backoff;
  stateStartedMs_ = millis();
}

void LeafLogSync::requestCancel() { cancelRequested_.store(true, std::memory_order_release); }

void LeafLogSync::requestPowerOn() {
  // HostOwned means the LUN was presented internally. hostMounted confirms that a real USB host
  // actually enumerated it, so a charge-only connection does not claim that USB needs closing.
  powerOnClosingUsb_ = sdcard.hostOwnsMassStorage() && leaf_usb::hostMounted();
  powerOnRequested_.store(true, std::memory_order_release);
  cancelRequested_.store(true, std::memory_order_release);
  if (state_ == State::Idle || state_ == State::HostOwned || state_ == State::Ejected) {
    state_ = State::AwaitingCenterIntent;
    stateStartedMs_ = millis();
  }
}

bool LeafLogSync::takePowerOnReady() {
  return powerOnReady_.exchange(false, std::memory_order_acq_rel);
}

bool LeafLogSync::interceptsChargingButtons() const {
  return state_ == State::CheckingEligibility || state_ == State::ConnectingWifi ||
         state_ == State::WaitingForTime || state_ == State::Uploading ||
         state_ == State::AwaitingCenterIntent || state_ == State::MassStorageUnavailable ||
         state_ == State::Backoff;
}

bool LeafLogSync::canSleepWhileCharging() const {
  return state_ == State::Idle || state_ == State::HostOwned || state_ == State::Ejected ||
         state_ == State::MassStorageUnavailable;
}

bool LeafLogSync::screenActive() const {
  if (state_ == State::Complete) return millis() - stateStartedMs_ < COMPLETION_POPUP_MS;
  if (state_ == State::Backoff || state_ == State::MassStorageUnavailable) {
    return millis() - stateStartedMs_ < NOTICE_POPUP_MS;
  }
  if (state_ == State::CheckingEligibility && sessionCompletionPending_) {
    return millis() - completionStartedMs_ < NOTICE_POPUP_MS;
  }
  return interceptsChargingButtons();
}

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
    case State::AwaitingCenterIntent:
      if (!powerOnRequested_.load(std::memory_order_acquire)) return "Hold center to turn on";
      return powerOnClosingUsb_ ? "Closing USB..." : "";
    case State::Backoff:
      return "Upload paused";
    case State::Complete:
      return "Uploads complete";
    case State::MassStorageUnavailable:
      return "USB drive unavailable";
    default:
      return "";
  }
}
