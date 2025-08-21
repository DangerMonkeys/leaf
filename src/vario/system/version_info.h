#pragma once

struct LeafVersionInfo {
  // Primary accessors (no heap)

  static const char* firmwareVersion();
  static const char* hardwareVariant();
  static const char* tagVersion();
  static const char* otaVersionsUrl();
  static const char* otaBinUrl();
  static bool otaAlwaysUpdate();
};
