#include "system/version_info.h"

#include "leaf_version.h"

// --- canonical C-string getters (no heap, no static ctors, no Arduino.h) ---
const char* LeafVersionInfo::firmwareVersion() { return FIRMWARE_VERSION; }
const char* LeafVersionInfo::hardwareVariant() { return HARDWARE_VARIANT; }
const char* LeafVersionInfo::tagVersion() { return TAG_VERSION; }
const char* LeafVersionInfo::otaVersionsUrl() { return OTA_VERSIONS_URL; }
const char* LeafVersionInfo::otaBinUrl() { return OTA_BIN_URL; }
bool LeafVersionInfo::otaAlwaysUpdate() { return OTA_ALWAYS_UPDATE; }
