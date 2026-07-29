#include "comms/ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

#include <string.h>
#include <stdexcept>

#include "diagnostics/heap_monitor.h"
#include "system/version_info.h"
#include "ui/audio/speaker.h"
#include "ui/settings/settings.h"

namespace {
  int versionNumberAt(const String& version, int partIndex) {
    int currentPart = 0;
    int value = 0;
    bool hasDigit = false;

    for (size_t i = 0; i <= version.length(); i++) {
      const char c = i < version.length() ? version[i] : '.';
      if (c >= '0' && c <= '9') {
        if (currentPart == partIndex) {
          value = value * 10 + c - '0';
          hasDigit = true;
        }
      } else {
        if (currentPart == partIndex) return hasDigit ? value : 0;
        if (c == '.') {
          currentPart++;
        } else {
          return 0;
        }
      }
    }

    return 0;
  }

  int compareTagVersions(const String& lhs, const String& rhs) {
    for (int i = 0; i < 3; i++) {
      const int lhsPart = versionNumberAt(lhs, i);
      const int rhsPart = versionNumberAt(rhs, i);
      if (lhsPart < rhsPart) return -1;
      if (lhsPart > rhsPart) return 1;
    }
    return 0;
  }

  String tagVersionFromReleaseTag(const char* releaseTag) {
    if (releaseTag == nullptr || releaseTag[0] == '\0') return "";
    if (releaseTag[0] == 'v' || releaseTag[0] == 'V') return String(releaseTag + 1);
    return String(releaseTag);
  }

  String getLatestReleaseTagVersion() {
    heap_monitor::checkpoint("ota-release-list-start");
    Serial.print("[OTA] Getting latest release list from ");
    Serial.println(LeafVersionInfo::otaReleasesApiUrl());

    HTTPClient http;
    http.begin(LeafVersionInfo::otaReleasesApiUrl());
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "leaf-firmware-ota");
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      heap_monitor::checkpoint("ota-release-list-http-fail");
      throw std::runtime_error(((String) "HTTP GET failed " + httpCode).c_str());
    }
    heap_monitor::checkpoint("ota-release-list-http-ok");

    String payload = http.getString();
    heap_monitor::checkpoint("ota-release-list-payload");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      heap_monitor::checkpoint("ota-release-list-json-fail");
      throw std::runtime_error(
          ((String) "GitHub releases JSON parse failed: " + error.c_str()).c_str());
    }
    heap_monitor::checkpoint("ota-release-list-json-ok");

    JsonArrayConst releases = doc.as<JsonArrayConst>();
    if (!releases.isNull() && releases.size() > 0) {
      String tagVersion = tagVersionFromReleaseTag(releases[0]["tag_name"]);
      if (!tagVersion.isEmpty()) {
        Serial.printf("[OTA] Latest release tag version is %s\n", tagVersion.c_str());
        heap_monitor::checkpoint("ota-release-list-match");
        return tagVersion;
      }
    }

    heap_monitor::checkpoint("ota-release-list-no-match");
    throw std::runtime_error("No GitHub release tag found.");
  }

  String getLatestStableTagVersion() {
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
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      heap_monitor::checkpoint("ota-version-json-fail");
      throw std::runtime_error(((String) "Version JSON parse failed: " + error.c_str()).c_str());
    }

    String tagVersion = doc["latest_tag_versions"][LeafVersionInfo::hardwareVariant()];
    Serial.printf("[OTA] Latest tag version for %s is %s\n", LeafVersionInfo::hardwareVariant(),
                  tagVersion.c_str());
    heap_monitor::checkpoint("ota-version-end");
    return tagVersion;
  }
}  // namespace

String getLatestTagVersion() {
  if (settings.dev_mode) return getLatestReleaseTagVersion();
  return getLatestStableTagVersion();
}

bool otaUpdateAvailable(const String& latestTagVersion) {
  if (LeafVersionInfo::otaAlwaysUpdate()) {
    return true;
  }

  const int tagComparison = compareTagVersions(latestTagVersion, LeafVersionInfo::tagVersion());
  if (tagComparison > 0) return true;
  if (tagComparison < 0) return false;

  if (!settings.dev_mode || latestTagVersion != LeafVersionInfo::tagVersion()) return false;

  const char* firmwareVersion = LeafVersionInfo::firmwareVersion();
  const char* buildMetadata = strchr(firmwareVersion, '+');
  const char* prerelease = strchr(firmwareVersion, '-');
  return prerelease != nullptr && (buildMetadata == nullptr || prerelease < buildMetadata);
}

/*
   Performs an over the air update.

   TODO:  Have this return a data type with meaningul information
   about the result
*/
void PerformOTAUpdate(const char* tag) {
  heap_monitor::checkpoint("ota-update-start");
  speaker.mute();
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
    speaker.unMute();
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
    speaker.unMute();
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
    speaker.unMute();
    throw std::runtime_error("Err finishing update");
  }

  delay(1000);
  ESP.restart();
}
