#pragma once

#include <Arduino.h>

namespace leaf_log_credentials {
  struct Snapshot {
    String token;
    String handle;
    String displayName;
    bool reconnectRequired = false;

    bool linked() const { return !token.isEmpty() && !reconnectRequired; }
  };

  Snapshot load();
  bool store(const String& token, const String& handle, const String& displayName);
  bool updateAccount(const String& handle, const String& displayName);
  void markReconnectRequired();
  void clear();
}  // namespace leaf_log_credentials
