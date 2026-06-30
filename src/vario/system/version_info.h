#pragma once

#include <stddef.h>

struct LeafVersionInfo {
  // Primary accessors (no heap)

  static const char* firmwareVersion();
  static const char* hardwareVariant();
  static const char* tagVersion();
  static const char* otaVersionsUrl();
  static const char* otaBinUrl();
  static bool otaAlwaysUpdate();

  static void firmwareDisplayVersion(char* out, size_t len);
  static void hardwareDisplayVersion(char* out, size_t len);
};
