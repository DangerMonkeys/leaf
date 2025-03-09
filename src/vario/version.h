#pragma once

// Defined in environment variables via src/scripts/versioning.py:
// FIRMWARE_VERSION: full semantic version (e.g., 0.9.0-9bba_dirty+h2.3.5)
// TAG_VERSION: latest tag version (e.g., 0.9.0)

// Where to check for the latest version
#define OTA_HOST "github.com"
#define OTA_BIN_FILENAME "/DangerMonkeys/leaf/releases/latest/download/firmware.bin"
#define OTA_VERSION_FILENAME "/DangerMonkeys/leaf/releases/latest/download/leaf.version"