#include "system/version_info.h"

#include <string.h>

#include "leaf_version.h"

namespace {
  const char* fallbackVersion() { return "unknown"; }

  void copyVersionSegment(char* out, size_t len, const char* segment, size_t segment_len,
                          bool strip_hardware_prefix) {
    if (!out || len == 0) return;

    out[0] = '\0';

    if (!segment) {
      segment = fallbackVersion();
      segment_len = strlen(segment);
    }

    if (strip_hardware_prefix && segment_len > 0 && (segment[0] == 'h' || segment[0] == 'H')) {
      segment++;
      segment_len--;
    }

    if (segment_len == 0) {
      segment = fallbackVersion();
      segment_len = strlen(segment);
    }

    size_t out_index = 0;
    if (segment[0] != 'v' && segment[0] != 'V') {
      out[out_index++] = 'v';
      if (out_index >= len) {
        out[len - 1] = '\0';
        return;
      }
    }

    const size_t remaining = len - out_index - 1;
    const size_t copy_len = segment_len < remaining ? segment_len : remaining;
    memcpy(out + out_index, segment, copy_len);
    out[out_index + copy_len] = '\0';
  }
}  // namespace

// --- canonical C-string getters (no heap, no static ctors, no Arduino.h) ---
const char* LeafVersionInfo::firmwareVersion() { return FIRMWARE_VERSION; }
const char* LeafVersionInfo::hardwareVariant() { return HARDWARE_VARIANT; }
const char* LeafVersionInfo::tagVersion() { return TAG_VERSION; }
const char* LeafVersionInfo::otaVersionsUrl() { return OTA_VERSIONS_URL; }
const char* LeafVersionInfo::otaBinUrl() { return OTA_BIN_URL; }
bool LeafVersionInfo::otaAlwaysUpdate() { return OTA_ALWAYS_UPDATE; }

void LeafVersionInfo::firmwareDisplayVersion(char* out, size_t len) {
  const char* combined = firmwareVersion();
  const char* split = strchr(combined, '+');
  copyVersionSegment(out, len, combined,
                     split ? static_cast<size_t>(split - combined) : strlen(combined), false);
}

void LeafVersionInfo::hardwareDisplayVersion(char* out, size_t len) {
  const char* combined = firmwareVersion();
  const char* split = strchr(combined, '+');
  if (split) {
    copyVersionSegment(out, len, split + 1, strlen(split + 1), true);
    return;
  }

  copyVersionSegment(out, len, hardwareVariant(), strlen(hardwareVariant()), true);
}
