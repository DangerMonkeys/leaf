#include "ota.h"

#include <HTTPClient.h>

#include <stdexcept>

#include "version.h"

String getLatestVersion() {
    HTTPClient http;
    String url = "https://" + String(OTA_HOST) + OTA_VERSION_FILENAME;
    http.begin(url);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        throw std::runtime_error(
            ((String) "HTTP GET failed with code " + httpCode).c_str());
    }

    String payload = http.getString();
    payload.trim();
    return payload;
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
        throw std::runtime_error(
            ((String) "HTTP GET failed with code " + httpCode).c_str());
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
        ESP.restart();
    } else {
        throw std::runtime_error("Error finishing firmware update");
    }

    delay(1000);
    ESP.restart();
}