#pragma once

#include <Update.h>
#include <WiFi.h>

// Gets the latest tag version for this hardware variant from latest release on Github
String getLatestTagVersion();

// Performs an over the air update
void PerformOTAUpdate(const char* tag);
