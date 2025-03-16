#include "ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

#include <stdexcept>

#include "settings.h"
#include "version.h"

String getLatestVersion() {
  HTTPClient http;
  String url = "https://" + String(OTA_HOST) + OTA_VERSIONS_FILENAME;
  http.begin(url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    throw std::runtime_error(((String) "HTTP GET failed with code " + httpCode).c_str());
  }

  String payload = http.getString();
  JsonDocument doc;
  deserializeJson(doc, payload);

  return doc["latest_tag_versions"][HARDWARE_VARIANT];
}

/*
   Performs an over the air update.

   TODO:  Have this return a data type with meaningul information
   about the result
*/
void PerformOTAUpdate() {
  HTTPClient http;

  http.begin(String("https://") + OTA_HOST + OTA_BIN_FILENAME);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  Serial.println("Starting OTA over HTTPS");
  auto httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    throw std::runtime_error(((String) "HTTP GET failed with code " + httpCode).c_str());
  }

  auto binarySize = http.getSize();
  Serial.print("Remote binary size: ");
  Serial.println(binarySize);

  auto payloadPtr = http.getStreamPtr();
  Update.begin(binarySize);
  if (Update.writeStream(*payloadPtr) != binarySize) {
    throw std::runtime_error("Error writing binary to flash");
  }

  Serial.println("OTA done!");

  if (Update.end()) {
    Serial.println("Update successfully completed. Rebooting.");
    BOOT_TO_ON = true;  // restart into 'on' state on reboot
    settings_save();
    ESP.restart();
  } else {
    throw std::runtime_error("Error finishing firmware update");
  }

  delay(1000);
  ESP.restart();
}