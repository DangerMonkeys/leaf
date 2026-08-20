#include "comms/leaf_log_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <WiFiClientSecure.h>

namespace leaf_log_client {
  namespace {
    class CancellableFileStream : public Stream {
     public:
      CancellableFileStream(File& file, std::atomic<bool>& cancelled,
                            std::atomic<bool>& urgentButtonPress, uint32_t deadlineMs)
          : file_(file),
            cancelled_(cancelled),
            urgentButtonPress_(urgentButtonPress),
            deadlineMs_(deadlineMs) {}

      int available() override {
        if (cancelled_.load(std::memory_order_acquire) ||
            urgentButtonPress_.load(std::memory_order_acquire)) {
          aborted_ = true;
          return -1;
        }
        if (static_cast<int32_t>(millis() - deadlineMs_) >= 0) {
          deadlineExceeded_ = true;
          aborted_ = true;
          return -1;
        }
        return file_.available();
      }
      int read() override { return file_.read(); }
      int peek() override { return file_.peek(); }
      void flush() override {}
      size_t write(uint8_t) override { return 0; }
      size_t readBytes(char* buffer, size_t length) override {
        if (available() < 0) return 0;
        return file_.readBytes(buffer, length);
      }
      bool aborted() const { return aborted_; }
      bool deadlineExceeded() const { return deadlineExceeded_; }

     private:
      File& file_;
      std::atomic<bool>& cancelled_;
      std::atomic<bool>& urgentButtonPress_;
      uint32_t deadlineMs_;
      bool aborted_ = false;
      bool deadlineExceeded_ = false;
    };
  }  // namespace

  UploadResult uploadIgc(const String& trackPath, const String& filename, const String& token,
                         std::atomic<bool>& cancelRequested, std::atomic<bool>& urgentButtonPress) {
    const uint32_t startedMs = millis();
    UploadResult result;
    File file = SD_MMC.open(trackPath, "r");
    if (!file) {
      result.diagnostic = "track_open_failed";
      result.elapsedMs = millis() - startedMs;
      return result;
    }
    result.fileSize = file.size();

    WiFiClientSecure client;
    client.setCACert(leafLogCaCertificate());
    HTTPClient http;
    if (!http.begin(client, String(leafLogBaseUrl()) + "/api/ingest")) {
      result.diagnostic = "http_begin_failed";
      result.elapsedMs = millis() - startedMs;
      file.close();
      return result;
    }
    http.setConnectTimeout(10000);
    http.setTimeout(15000);
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("X-Filename", filename);
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("Accept", "application/json");

    CancellableFileStream stream(file, cancelRequested, urgentButtonPress, millis() + 60000);
    result.httpStatus = http.sendRequest("POST", &stream, result.fileSize);
    file.close();

    if (stream.deadlineExceeded()) {
      result.diagnostic = "stream_deadline";
      result.elapsedMs = millis() - startedMs;
      http.end();
      return result;
    }
    if (stream.aborted() || cancelRequested.load(std::memory_order_acquire) ||
        urgentButtonPress.load(std::memory_order_acquire)) {
      result.outcome = UploadOutcome::Cancelled;
      result.diagnostic = "cancelled";
      result.elapsedMs = millis() - startedMs;
      http.end();
      return result;
    }
    if (result.httpStatus == 401) {
      result.outcome = UploadOutcome::Unauthorized;
      result.diagnostic = "unauthorized";
    } else if (result.httpStatus == 400) {
      result.outcome = UploadOutcome::Invalid;
      result.diagnostic = "invalid_igc";
    } else if (result.httpStatus == 413) {
      result.outcome = UploadOutcome::TooLarge;
      result.diagnostic = "too_large";
    } else if (result.httpStatus == 200) {
      // Railway serves Next.js JSON responses with chunked transfer encoding. HTTPClient's
      // raw network stream still contains the chunk framing, while getString() decodes it.
      const String responseBody = http.getString();
      result.responseSize = responseBody.length();
      JsonDocument response;
      const DeserializationError error = deserializeJson(response, responseBody);
      result.flightId = response["flightId"] | "";
      JsonObject account = response["account"];
      result.accountHandle = account["handle"] | "";
      result.accountDisplayName = account["displayName"] | "";
      if (error) {
        result.diagnostic = "response_json_invalid";
      } else if (result.flightId.isEmpty()) {
        result.diagnostic = "response_flight_id_missing";
      } else {
        result.outcome = UploadOutcome::Delivered;
        result.diagnostic = "delivered";
      }
    } else if (result.httpStatus <= 0) {
      result.diagnostic = "http_transport_error";
    } else {
      result.diagnostic = "http_status";
    }
    result.elapsedMs = millis() - startedMs;
    http.end();
    return result;
  }
}  // namespace leaf_log_client
