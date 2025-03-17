#include "ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

#include <stdexcept>

#include "settings.h"
#include "version.h"

String getLatestTagVersion() {
  Serial.print("[OTA] Getting latest tag version from ");
  Serial.println(OTA_VERSIONS_URL);
  HTTPClient http;
  http.begin(OTA_VERSIONS_URL);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    throw std::runtime_error(((String) "HTTP GET failed " + httpCode).c_str());
  }

  String payload = http.getString();
  JsonDocument doc;
  deserializeJson(doc, payload);

  String tagVersion = doc["latest_tag_versions"][HARDWARE_VARIANT];
  Serial.printf("[OTA] Latest tag version for %s is %s\n", HARDWARE_VARIANT, tagVersion);
  return tagVersion;
}

/*
   Performs an over the air update.

   TODO:  Have this return a data type with meaningul information
   about the result
*/
void PerformOTAUpdate(const char* tag) {
  char url[120];
  snprintf(url, sizeof(url), OTA_BIN_URL, tag);
  Serial.print("[OTA] Starting OTA from ");
  Serial.println(url);
  HTTPClient http;
  http.begin(url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  auto httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    throw std::runtime_error(((String) "HTTP GET failed " + httpCode).c_str());
  }

  auto binarySize = http.getSize();
  Serial.print("[OTA] Remote binary size: ");
  Serial.println(binarySize);

  auto payloadPtr = http.getStreamPtr();
  Update.begin(binarySize);
  if (Update.writeStream(*payloadPtr) != binarySize) {
    throw std::runtime_error("Err writing bin->flash");
  }

  Serial.println("[OTA] Done!");

  if (Update.end()) {
    Serial.println("[OTA] Update successfully completed. Rebooting.");
    BOOT_TO_ON = true;  // restart into 'on' state on reboot
    settings_save();
    ESP.restart();
  } else {
    throw std::runtime_error("Err finishing update");
  }

  delay(1000);
  ESP.restart();
}