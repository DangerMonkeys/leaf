#pragma once

// This build/release version
#ifndef LEAF_VERSION
#define LEAF_VERSION "DEV"
#endif

#ifndef HARDWARE_NAME
#define HARDWARE_NAME "UNKNOWN"
#endif

// Where to check for the latest version
#define OTA_HOST "github.com"
#define OTA_BIN_FILENAME "/DangerMonkeys/leaf/releases/latest/download/fw-" HARDWARE_NAME ".bin"
#define OTA_VERSION_FILENAME "/DangerMonkeys/leaf/releases/latest/download/leaf.version"