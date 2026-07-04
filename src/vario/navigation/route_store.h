#pragma once

#include <Arduino.h>

namespace route_store {

  struct ImportResult {
    bool ok = false;
    String error;
    String path;
    uint8_t points = 0;
    bool active = false;
  };

  constexpr const char* directoryPath() { return "/routes"; }
  constexpr const char* activeRoutePath() { return "/routes/active.json"; }

  bool importRouteText(const String& name, const String& data, bool activate, ImportResult& result);
  bool loadRouteFile(const String& path, bool activate);
  bool loadActiveRoute();
  bool clearActiveRoute();

}  // namespace route_store
