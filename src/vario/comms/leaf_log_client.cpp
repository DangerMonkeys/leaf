#include "comms/leaf_log_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <WiFiClientSecure.h>

namespace leaf_log_client {
  namespace {
    class CancellableFileStream : public Stream {
     public:
      CancellableFileStream(File& file, std::atomic<bool>& cancelled, uint32_t deadlineMs)
          : file_(file), cancelled_(cancelled), deadlineMs_(deadlineMs) {}

      int available() override {
        if (cancelled_.load(std::memory_order_acquire) ||
            static_cast<int32_t>(millis() - deadlineMs_) >= 0) {
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

     private:
      File& file_;
      std::atomic<bool>& cancelled_;
      uint32_t deadlineMs_;
      bool aborted_ = false;
    };
  }  // namespace

  UploadResult uploadIgc(const String& trackPath, const String& filename, const String& token,
                         std::atomic<bool>& cancelRequested) {
    UploadResult result;
    File file = SD_MMC.open(trackPath, "r");
    if (!file) return result;
    const size_t fileSize = file.size();

    WiFiClientSecure client;
    client.setCACert(leafLogCaCertificate());
    HTTPClient http;
    if (!http.begin(client, String(leafLogBaseUrl()) + "/api/ingest")) {
      file.close();
      return result;
    }
    http.setConnectTimeout(10000);
    http.setTimeout(15000);
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("X-Filename", filename);
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("Accept", "application/json");

    CancellableFileStream stream(file, cancelRequested, millis() + 60000);
    result.httpStatus = http.sendRequest("POST", &stream, fileSize);
    file.close();

    if (stream.aborted() || cancelRequested.load(std::memory_order_acquire)) {
      result.outcome = UploadOutcome::Cancelled;
      http.end();
      return result;
    }
    if (result.httpStatus == 401) {
      result.outcome = UploadOutcome::Unauthorized;
    } else if (result.httpStatus == 400) {
      result.outcome = UploadOutcome::Invalid;
    } else if (result.httpStatus == 413) {
      result.outcome = UploadOutcome::TooLarge;
    } else if (result.httpStatus == 200) {
      JsonDocument response;
      const DeserializationError error = deserializeJson(response, http.getStream());
      result.flightId = response["flightId"] | "";
      JsonObject account = response["account"];
      result.accountHandle = account["handle"] | "";
      result.accountDisplayName = account["displayName"] | "";
      if (!error && !result.flightId.isEmpty()) result.outcome = UploadOutcome::Delivered;
    }
    http.end();
    return result;
  }
}  // namespace leaf_log_client
