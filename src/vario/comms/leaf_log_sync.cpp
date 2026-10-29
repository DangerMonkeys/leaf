#include "comms/leaf_log_sync.h"
#include "logbook/logbook_io.h"

#include <SD_MMC.h>
#include <WiFi.h>
#include <freertos/task.h>
#include <time.h>
#include <cerrno>
#include <cstring>

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
  constexpr uint32_t SCAN_SLICE_MS = 10;
  constexpr uint8_t SCAN_SLICE_ENTRIES = 32;
  constexpr const char* SCAN_PATHS = "/.leaf-log-scan.tmp";
  constexpr const char* PENDING_PATHS = "/.leaf-log-pending.tmp";
  constexpr size_t MAX_QUEUED_PATH_BYTES = 512;

  // Length prefixes also support filenames containing newlines. These lists are scratch data,
  // rebuilt each session; the upload/rejection markers in the logbook remain authoritative.
  bool writeQueuedPath(File& file, const String& path, size_t& bytes) {
    const size_t length = path.length();
    if (length == 0 || length > MAX_QUEUED_PATH_BYTES || length + 2 > SIZE_MAX - bytes)
      return false;
    const uint8_t prefix[] = {static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8)};
    if (file.write(prefix, sizeof(prefix)) != sizeof(prefix) ||
        file.write(reinterpret_cast<const uint8_t*>(path.c_str()), length) != length)
      return false;
    bytes += sizeof(prefix) + length;
    return true;
  }

  // File::flush/close do not report errors. Check the reopened length and consume the
  // expected record count as well, so a lost tail cannot masquerade as a clean EOF.
  bool reopenQueue(File& file, const char* path, size_t expectedBytes) {
    file.close();
    file = SD_MMC.open(path, "r");
    return file && !file.isDirectory() && file.size() == expectedBytes;
  }

  bool queueConsumed(File& file, size_t expectedBytes) {
    if (!file || file.position() != expectedBytes || file.size() != expectedBytes) return false;
    // ESP32 File caches the size of read-only files; probe EOF rather than trusting available().
    errno = 0;
    return file.read() == -1 && errno == 0;
  }

  bool readQueuedPath(File& file, String& path) {
    uint8_t prefix[2];
    if (file.read(prefix, sizeof(prefix)) != sizeof(prefix)) return false;
    const size_t length = prefix[0] | (static_cast<size_t>(prefix[1]) << 8);
    if (length == 0 || length > MAX_QUEUED_PATH_BYTES) return false;
    char buffer[MAX_QUEUED_PATH_BYTES + 1];
    if (file.readBytes(buffer, length) != length) return false;
    buffer[length] = '\0';
    path = buffer;
    return path.length() == length && LogbookStore::isLogbookJsonPath(path);
  }
}  // namespace

LeafLogSync leafLogSync;

void LeafLogSync::beginSession(bool fromEject) {
  LogbookStore::beginRepairSession(false);
  // Uploads no longer block input polling. A first center tap asks for confirmation,
  // so the old interrupt-to-abort shortcut must not cancel the transport.
  buttons.disarmUrgentPressCapture();
  cancelConfirmation_ = false;
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
  clearSessionFiles();
  current_ = LeafLogCandidate();
  pendingCount_ = 0;
  pendingPathsRemaining_ = 0;
  scanPathsRemaining_ = 0;
  scanQueueBytes_ = pendingQueueBytes_ = 0;
  wifiAttemptStarted_ = false;
  timeStarted_ = false;
  state_ = State::CheckingEligibility;
  stateStartedMs_ = millis();

  const auto credential = leaf_log_credentials::load();
  if (!settings.labs_leafLog || !credential.linked() || !sdcard.firmwareCanAccessFilesystem()) {
    finishToMassStorage();
    return;
  }
  errno = 0;
  scanDirectory_ = SD_MMC.open(LogbookStore::directoryPath());
  if (!scanDirectory_ || !scanDirectory_.isDirectory()) {
    if (!scanDirectory_ && errno == ENOENT)
      finishToMassStorage();
    else
      invalidateEligibilityScan("scan_queue_directory_open_failed");
    return;
  }
  scanPaths_ = SD_MMC.open(SCAN_PATHS, "w", true);
  if (!scanPaths_ || scanPaths_.isDirectory()) handleTransientFailure("scan_queue_open_failed");
}

void LeafLogSync::clearSessionFiles() {
  if (scanDirectory_) scanDirectory_.close();
  if (scanPaths_) scanPaths_.close();
  if (pendingPaths_) pendingPaths_.close();
  // Never touch the filesystem after it has been handed to a USB host.
  if (sdcard.firmwareCanAccessFilesystem()) {
    if (SD_MMC.exists(SCAN_PATHS)) SD_MMC.remove(SCAN_PATHS);
    if (SD_MMC.exists(PENDING_PATHS)) SD_MMC.remove(PENDING_PATHS);
  }
}

void LeafLogSync::invalidateEligibilityScan(const char* reason) {
  // Scratch lists can always be rebuilt. Persisted delivery markers prevent re-uploading
  // flights already completed before a later queue failure.
  sessionTotalKnown_ = false;
  sessionCompletionPending_ = false;
  completedCount_ = sessionTotalCount_ = 0;
  current_ = LeafLogCandidate();
  handleTransientFailure(reason);
}

void LeafLogSync::scanEligibility() {
  logbook_io::Guard transaction;
  if (!transaction) return;
  if (!sdcard.firmwareCanAccessFilesystem()) {
    finishToMassStorage();
    return;
  }
  if (sessionTotalKnown_) {
    selectNextUpload();
    return;
  }

  const uint32_t startedMs = millis();
  for (uint8_t entries = 0; entries < SCAN_SLICE_ENTRIES; ++entries) {
    // A single filesystem operation may exceed the budget; yield/check cancellation between them.
    if (cancelRequested_.load(std::memory_order_acquire) ||
        powerOnRequested_.load(std::memory_order_acquire)) {
      return;
    }
    if (entries > 0 && millis() - startedMs >= SCAN_SLICE_MS) return;

    String path;
    if (scanDirectory_) {
      errno = 0;
      path = scanDirectory_.getNextFileName();
      const int directoryError = errno;
      if (!path.isEmpty()) {
        String logicalPath;
        if (logbook_io::candidatePath(path, logicalPath)) {
          if (scanPathsRemaining_ == UINT32_MAX ||
              !writeQueuedPath(scanPaths_, logicalPath, scanQueueBytes_)) {
            invalidateEligibilityScan("scan_queue_write_failed");
            return;
          }
          ++scanPathsRemaining_;
        }
        continue;
      }
      if (directoryError) {
        invalidateEligibilityScan("scan_queue_directory_read_failed");
        return;
      }
      // Close the directory before recovery or result writes can rename its entries.
      scanDirectory_.close();
      if (!reopenQueue(scanPaths_, SCAN_PATHS, scanQueueBytes_)) {
        invalidateEligibilityScan("scan_queue_incomplete");
        return;
      }
      pendingPaths_ = SD_MMC.open(PENDING_PATHS, "w", true);
      if (!pendingPaths_ || pendingPaths_.isDirectory()) {
        invalidateEligibilityScan("pending_queue_open_failed");
        return;
      }
    }

    if (scanPathsRemaining_ == 0) {
      if (!queueConsumed(scanPaths_, scanQueueBytes_) ||
          !reopenQueue(pendingPaths_, PENDING_PATHS, pendingQueueBytes_)) {
        invalidateEligibilityScan("pending_queue_incomplete");
        return;
      }
      scanPaths_.close();
      SD_MMC.remove(SCAN_PATHS);
      pendingPathsRemaining_ = pendingCount_;
      sessionTotalCount_ = pendingCount_;
      sessionTotalKnown_ = true;
      return;
    }
    if (!readQueuedPath(scanPaths_, path)) {
      invalidateEligibilityScan("scan_queue_read_failed");
      return;
    }
    --scanPathsRemaining_;
    LeafLogCandidate candidate;
    if (!LogbookStore::classifyForLeafLog(path, candidate)) continue;
    if (candidate.disposition == LeafLogCandidate::Disposition::Rejected &&
        !candidate.rejectionReason.isEmpty() && !candidate.rejectionPersisted) {
      if (!LogbookStore::recordLeafLogRejection(candidate.logbookPath, candidate.rejectionReason)) {
        handleTransientFailure("record_rejection_failed");
        return;
      }
    } else if (candidate.disposition == LeafLogCandidate::Disposition::Pending &&
               pendingCount_ < MAX_SESSION_FLIGHTS) {
      if (pendingCount_ == UINT16_MAX ||
          !writeQueuedPath(pendingPaths_, candidate.logbookPath, pendingQueueBytes_)) {
        invalidateEligibilityScan("pending_queue_write_failed");
        return;
      }
      pendingCount_++;
    }
  }
}

void LeafLogSync::selectNextUpload() {
  if (pendingPathsRemaining_ == 0) {
    if (!queueConsumed(pendingPaths_, pendingQueueBytes_))
      invalidateEligibilityScan("pending_queue_incomplete");
    else
      finishToMassStorage();
    return;
  }
  // Keep the read position until classification succeeds, so retries cannot drop an entry.
  const size_t position = pendingPaths_.position();
  String path;
  if (!readQueuedPath(pendingPaths_, path)) {
    invalidateEligibilityScan("pending_queue_read_failed");
    return;
  }
  if (!LogbookStore::classifyForLeafLog(path, current_)) {
    if (!pendingPaths_.seek(position)) {
      invalidateEligibilityScan("pending_queue_seek_failed");
      return;
    }
    current_ = LeafLogCandidate();
    handleTransientFailure("pending_entry_read_failed");
    return;
  }
  if (current_.disposition != LeafLogCandidate::Disposition::Pending) {
    if (current_.disposition == LeafLogCandidate::Disposition::Rejected &&
        !current_.rejectionPersisted &&
        !LogbookStore::recordLeafLogRejection(current_.logbookPath, current_.rejectionReason)) {
      if (!pendingPaths_.seek(position)) {
        invalidateEligibilityScan("pending_queue_seek_failed");
        return;
      }
      current_ = LeafLogCandidate();
      handleTransientFailure("record_rejection_failed");
      return;
    }
    --pendingPathsRemaining_;
    if (!recordCompletedItem()) return;
    current_ = LeafLogCandidate();
    return;
  }
  --pendingPathsRemaining_;
  startCurrentUpload();
}

void LeafLogSync::startCurrentUpload() {
  const auto credential = leaf_log_credentials::load();
  if (!settings.labs_leafLog || !credential.linked() || !sdcard.firmwareCanAccessFilesystem()) {
    finishToMassStorage();
    return;
  }
  wifiAttemptStarted_ = false;
  timeStarted_ = false;
  if (!sdcard.reserveForFirmwareUpload() &&
      sdcard.ownership() != SDCardOwnership::FirmwareUploading) {
    finishToMassStorage();
    return;
  }
  const bool wifiReady = WiFi.status() == WL_CONNECTED && !diagnostic_network.connected();
  state_ = wifiReady
               ? (time(nullptr) >= VALID_TIME_EPOCH ? State::Uploading : State::WaitingForTime)
               : State::ConnectingWifi;
  stateStartedMs_ = millis();
}

void LeafLogSync::update() {
  if (uploadRunning_) {
    // Do not hand the card to USB, disconnect WiFi or overwrite the request while
    // the worker can still be using it. Cancel/power-on flags interrupt its stream.
    if (!uploadDone_.load(std::memory_order_acquire)) return;
    uploadRunning_ = false;
    finishUpload(uploadResult_);
    if (powerOnReady_.load(std::memory_order_acquire)) return;
  }
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

  if (cancelRequested_.load(std::memory_order_acquire)) {
    finishRequestedExit();
    return;
  }

  switch (state_) {
    case State::CheckingEligibility:
      scanEligibility();
      break;
    case State::ConnectingWifi: {
      if (WiFi.status() == WL_CONNECTED && !diagnostic_network.connected()) {
        state_ = time(nullptr) >= VALID_TIME_EPOCH ? State::Uploading : State::WaitingForTime;
        stateStartedMs_ = millis();
        break;
      }

      if (!wifiAttemptStarted_) {
        leaf_wifi::attemptSavedNetworkConnection();
        wifiAttemptStarted_ = true;
      }

      const bool connectionInProgress = leaf_wifi::savedNetworkConnectionInProgress();
      if (WiFi.status() == WL_CONNECTED && !diagnostic_network.connected()) {
        state_ = time(nullptr) >= VALID_TIME_EPOCH ? State::Uploading : State::WaitingForTime;
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
    case State::Uploading:
      launchUpload();
      break;
    case State::Backoff:
      if (static_cast<int32_t>(millis() - retryAtMs_) >= 0) {
        if (!sessionTotalKnown_)
          beginEligibilityScan();
        else if (!current_.logbookPath.isEmpty())
          startCurrentUpload();
        else
          state_ = State::CheckingEligibility;
      }
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
  cancelConfirmation_ = false;
  clearSessionFiles();
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
  cancelConfirmation_ = false;
  clearSessionFiles();
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

bool LeafLogSync::recordCompletedItem() {
  completedCount_++;
  if (sessionTotalKnown_ && sessionTotalCount_ > 0 && completedCount_ >= sessionTotalCount_) {
    if (pendingPathsRemaining_ != 0 || !queueConsumed(pendingPaths_, pendingQueueBytes_)) {
      invalidateEligibilityScan("pending_queue_incomplete");
      return false;
    }
    sessionCompletionPending_ = true;
    completionStartedMs_ = millis();
  }
  return true;
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
  cancelConfirmation_ = false;
  buttons.disarmUrgentPressCapture();
  buttons.suppressEventsUntilRelease();
  clearSessionFiles();
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
  cancelConfirmation_ = false;
  const bool suppressButton = buttons.urgentPressLatched();
  buttons.disarmUrgentPressCapture();
  if (suppressButton) buttons.suppressEventsUntilRelease();
  clearSessionFiles();
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
  if (!sessionTotalKnown_) clearSessionFiles();
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
  const bool storageFailure =
      reason && (strncmp(reason, "record_", 7) == 0 || strncmp(reason, "pending_", 8) == 0 ||
                 strncmp(reason, "scan_queue_", 11) == 0 || strncmp(reason, "track_", 6) == 0);
  // Reporting a storage failure must not recursively write to the same failing card.
  if (!storageFailure)
    diagnostic_logs::appendSystemEvent("leaf_log", "retry", detail, "delay_ms", delayMs, true);
  state_ = State::Backoff;
  stateStartedMs_ = millis();
}

void LeafLogSync::requestCancel() {
  cancelConfirmation_ = false;
  cancelRequested_.store(true, std::memory_order_release);
}

bool LeafLogSync::cancelConfirmation() const {
  return cancelConfirmation_ && !sessionCompletionPending_ && !powerOnPending() &&
         millis() - cancelConfirmationStartedMs_ < 5000;
}

void LeafLogSync::requestChargingClick() {
  if (powerOnPending()) return;
  if (massStorageUnavailable()) {
    requestCancel();  // Retry the USB handoff, without restarting the upload session.
  } else if (interceptsChargingButtons() && !sessionCompletionPending_) {
    if (cancelConfirmation()) {
      requestCancel();
    } else {
      cancelConfirmation_ = true;
      cancelConfirmationStartedMs_ = millis();
    }
  }
}

void LeafLogSync::requestPowerOn() {
  cancelConfirmation_ = false;
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
  if (powerOnPending() || state_ == State::Backoff) return true;
  if (state_ == State::Complete) return millis() - stateStartedMs_ < COMPLETION_POPUP_MS;
  if (state_ == State::MassStorageUnavailable) {
    return millis() - stateStartedMs_ < NOTICE_POPUP_MS;
  }
  if (state_ == State::CheckingEligibility && sessionCompletionPending_) {
    return millis() - completionStartedMs_ < NOTICE_POPUP_MS;
  }
  return interceptsChargingButtons();
}

LeafLogSync::ScreenPhase LeafLogSync::screenPhase() const {
  if (sessionCompletionPending_ || completionNotice()) return ScreenPhase::Complete;
  if (retryPending()) return ScreenPhase::Paused;
  if (state_ == State::ConnectingWifi) return ScreenPhase::Wifi;
  if (state_ == State::WaitingForTime) return ScreenPhase::Connecting;
  if (preparingNext() || uploading()) return ScreenPhase::Transfer;
  return ScreenPhase::Scanning;
}

void LeafLogSync::launchUpload() {
  uploadPath_ = current_.trackPath;
  uploadFilename_ = current_.trackFilename;
  uploadToken_ = leaf_log_credentials::load().token;
  uploadDone_.store(false, std::memory_order_release);
  uploadRunning_ = true;
  if (xTaskCreate(uploadTask, "LeafLogUpload", 12 * 1024, this, 10, nullptr) != pdPASS) {
    uploadRunning_ = false;
    handleTransientFailure("upload_task_start_failed");
  }
}

void LeafLogSync::uploadTask(void* context) {
  auto* self = static_cast<LeafLogSync*>(context);
  self->uploadResult_ =
      leaf_log_client::uploadIgc(self->uploadPath_, self->uploadFilename_, self->uploadToken_,
                                 self->cancelRequested_, self->powerOnRequested_);
  self->uploadDone_.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

const char* LeafLogSync::statusLine() const {
  switch (state_) {
    case State::CheckingEligibility:
      return "Checking flights...";
    case State::ConnectingWifi:
      return "Starting WiFi...";
    case State::WaitingForTime:
      return "Connecting...";
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

void LeafLogSync::finishUpload(const leaf_log_client::UploadResult& result) {
  if (result.outcome == leaf_log_client::UploadOutcome::Delivered) {
    if (!LogbookStore::recordLeafLogFlightId(current_.logbookPath, result.flightId)) {
      handleTransientFailure("record_flight_id_failed", result.httpStatus, result.elapsedMs,
                             result.fileSize, result.responseSize);
      return;
    }
    if (!result.accountHandle.isEmpty() && !result.accountDisplayName.isEmpty()) {
      leaf_log_credentials::updateAccount(result.accountHandle, result.accountDisplayName);
    }
    if (!recordCompletedItem()) return;
    retryIndex_ = 0;
    current_ = LeafLogCandidate();
    state_ = State::CheckingEligibility;
  } else if (result.outcome == leaf_log_client::UploadOutcome::Unauthorized) {
    leaf_log_credentials::markReconnectRequired();
    if (powerOnPending())
      finishForPowerOn();
    else
      finishToMassStorage();
  } else if (result.outcome == leaf_log_client::UploadOutcome::Invalid ||
             result.outcome == leaf_log_client::UploadOutcome::TooLarge) {
    if (!LogbookStore::recordLeafLogRejection(
            current_.logbookPath, result.outcome == leaf_log_client::UploadOutcome::Invalid
                                      ? "invalid_igc"
                                      : "too_large")) {
      handleTransientFailure("record_rejection_failed", result.httpStatus, result.elapsedMs,
                             result.fileSize, result.responseSize);
      return;
    }
    if (!recordCompletedItem()) return;
    retryIndex_ = 0;
    current_ = LeafLogCandidate();
    state_ = State::CheckingEligibility;
  } else if (result.outcome == leaf_log_client::UploadOutcome::Cancelled) {
    finishRequestedExit();
  } else {
    handleTransientFailure(result.diagnostic, result.httpStatus, result.elapsedMs, result.fileSize,
                           result.responseSize, result.transportDetail);
  }
}
