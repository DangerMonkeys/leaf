#pragma once

// The values here do not matter; they are set automatically
// at build time via src/scripts/versioning.py:
// ---------------------------------------------------------------
// Full semantic version (e.g., "0.9.0-9bbad.dev+h2.3.5")
#define FIRMWARE_VERSION "0.0.0+build.script.error"
// Latest tag version (e.g., "0.9.0")
#define TAG_VERSION "0.0.0"
// Hardware variant name (e.g., "leaf_3_2_5")
#define HARDWARE_VARIANT "unknown"
// ---------------------------------------------------------------

// Where to check for the latest version
#define OTA_ORG "DangerMonkeys"

#define OTA_BIN_PATH OTA_ORG "/leaf/releases/download/v%s/firmware-" HARDWARE_VARIANT ".bin"
#define OTA_BIN_URL "https://github.com/" OTA_BIN_PATH

#define OTA_VERSIONS_PATH OTA_ORG "/leaf/releases/latest/download/latest_versions.json"
#define OTA_VERSIONS_URL "https://github.com/" OTA_VERSIONS_PATH

// If true, always perform OTA update regardless of whether already on latest version
#define OTA_ALWAYS_UPDATE false
