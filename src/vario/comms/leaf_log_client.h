#pragma once

#include <Arduino.h>
#include <atomic>

const char* leafLogBaseUrl();
const char* leafLogCaCertificate();

namespace leaf_log_client {
  enum class UploadOutcome : uint8_t {
    Delivered,
    Unauthorized,
    Invalid,
    TooLarge,
    TransientFailure,
    Cancelled,
  };

  struct UploadResult {
    UploadOutcome outcome = UploadOutcome::TransientFailure;
    int httpStatus = 0;
    const char* diagnostic = "not_started";
    uint32_t elapsedMs = 0;
    size_t fileSize = 0;
    size_t responseSize = 0;
    String flightId;
    String accountHandle;
    String accountDisplayName;
  };

  UploadResult uploadIgc(const String& trackPath, const String& filename, const String& token,
                         std::atomic<bool>& cancelRequested, std::atomic<bool>& urgentButtonPress);
}  // namespace leaf_log_client
