#pragma once

// The values here do not matter; they are set automatically
// at build time via src/scripts/versioning.py:
// ---------------------------------------------------------------
// Full semantic version (e.g., "0.9.0-9bbad.dev+h2.3.5")
#define FIRMWARE_VERSION "0.0.9-a81+h3.2.5"
// Latest tag version (e.g., "0.9.0")
#define TAG_VERSION "0.0.9"
// Hardware variant name (e.g., "leaf_3_2_5")
#define HARDWARE_VARIANT "leaf_3_2_5"
// ---------------------------------------------------------------

// Where to check for the latest version
#define OTA_HOST "github.com"
#define OTA_BIN_FILENAME \
  "/DangerMonkeys/leaf/releases/latest/download/firmware-" HARDWARE_VARIANT ".bin"
// TODO: Get latest tag version for current hardware variant from latest_versions.json instead of
// leaf.version
#define OTA_VERSION_FILENAME "/DangerMonkeys/leaf/releases/latest/download/leaf.version"
