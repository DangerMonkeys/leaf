#pragma once

#include <Update.h>
#include <WiFi.h>

// Gets the latest tag version for this hardware variant from latest release on Github
String getLatestTagVersion();

// Determines whether the latest OTA tag should be installed over the current firmware.
bool otaUpdateAvailable(const String& latestTagVersion);

// Performs an over the air update
void PerformOTAUpdate(const char* tag);
