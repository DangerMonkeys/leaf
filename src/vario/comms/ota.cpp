#include "comms/ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

#include <stdexcept>

#include "diagnostics/heap_monitor.h"
#include "system/version_info.h"
#include "ui/settings/settings.h"

String getLatestTagVersion() {
  heap_monitor::checkpoint("ota-version-start");
  Serial.print("[OTA] Getting latest tag version from ");
  Serial.println(LeafVersionInfo::otaVersionsUrl());
  HTTPClient http;
  http.begin(LeafVersionInfo::otaVersionsUrl());
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    heap_monitor::checkpoint("ota-version-http-fail");
    throw std::runtime_error(((String) "HTTP GET failed " + httpCode).c_str());
  }
  heap_monitor::checkpoint("ota-version-http-ok");

  String payload = http.getString();
  heap_monitor::checkpoint("ota-version-payload");
  JsonDocument doc;
  deserializeJson(doc, payload);

  String tagVersion = doc["latest_tag_versions"][LeafVersionInfo::hardwareVariant()];
  Serial.printf("[OTA] Latest tag version for %s is %s\n", LeafVersionInfo::hardwareVariant(),
                tagVersion);
  heap_monitor::checkpoint("ota-version-end");
  return tagVersion;
}

/*
   Performs an over the air update.

   TODO:  Have this return a data type with meaningul information
   about the result
*/
void PerformOTAUpdate(const char* tag) {
  heap_monitor::checkpoint("ota-update-start");
  char url[120];
  snprintf(url, sizeof(url), LeafVersionInfo::otaBinUrl(), tag);
  Serial.print("[OTA] Starting OTA from ");
  Serial.println(url);
  HTTPClient http;
  http.begin(url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  auto httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    heap_monitor::checkpoint("ota-update-http-fail");
    throw std::runtime_error(((String) "HTTP GET failed " + httpCode).c_str());
  }
  heap_monitor::checkpoint("ota-update-http-ok");

  auto binarySize = http.getSize();
  Serial.print("[OTA] Remote binary size: ");
  Serial.println(binarySize);

  auto payloadPtr = http.getStreamPtr();
  Update.begin(binarySize);
  heap_monitor::checkpoint("ota-update-begin");
  if (Update.writeStream(*payloadPtr) != binarySize) {
    heap_monitor::checkpoint("ota-update-write-fail");
    throw std::runtime_error("Err writing bin->flash");
  }
  heap_monitor::checkpoint("ota-update-written");

  Serial.println("[OTA] Done!");

  if (Update.end()) {
    heap_monitor::checkpoint("ota-update-end-ok");
    Serial.println("[OTA] Update successfully completed. Rebooting.");
    settings.boot_toOnState = true;  // restart into 'on' state on reboot
    settings.save();
    ESP.restart();
  } else {
    heap_monitor::checkpoint("ota-update-end-fail");
    throw std::runtime_error("Err finishing update");
  }

  delay(1000);
  ESP.restart();
}
