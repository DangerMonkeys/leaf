#pragma once

// Defined in environment variables via src/scripts/versioning.py:
// FIRMWARE_VERSION: full semantic version (e.g., "0.9.0-9bbad.dev+h2.3.5")
// TAG_VERSION: latest tag version (e.g., "0.9.0")
// HARDWARE_VARIANT: hardware variant name (e.g., "leaf_3_2_5")

// Where to check for the latest version
#define OTA_HOST "github.com"
#define OTA_BIN_FILENAME \
  "/DangerMonkeys/leaf/releases/latest/download/firmware-" HARDWARE_VARIANT ".bin"
// TODO: Get latest tag version for current hardware variant from latest_versions.json instead of
// leaf.version
#define OTA_VERSION_FILENAME "/DangerMonkeys/leaf/releases/latest/download/leaf.version"
