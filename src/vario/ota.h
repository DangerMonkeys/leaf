#pragma once

#include <Update.h>
#include <WiFi.h>

// Gets the latest version from Github's version!
String getLatestVersion();

// Performs an over the air update
void PerformOTAUpdate();
