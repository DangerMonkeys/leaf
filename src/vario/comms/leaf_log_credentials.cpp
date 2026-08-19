#include "comms/leaf_log_credentials.h"

#include <Preferences.h>

namespace leaf_log_credentials {
  namespace {
    constexpr const char* NAMESPACE = "leafLog";
    constexpr const char* TOKEN_KEY = "token";
    constexpr const char* HANDLE_KEY = "handle";
    constexpr const char* DISPLAY_KEY = "display";
    constexpr const char* RECONNECT_KEY = "reconnect";
  }  // namespace

  Snapshot load() {
    Snapshot result;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, true)) return result;
    result.token = prefs.getString(TOKEN_KEY);
    result.handle = prefs.getString(HANDLE_KEY);
    result.displayName = prefs.getString(DISPLAY_KEY);
    result.reconnectRequired = prefs.getBool(RECONNECT_KEY, false);
    prefs.end();
    return result;
  }

  bool store(const String& token, const String& handle, const String& displayName) {
    if (!token.startsWith("llk_") || token.length() < 5) return false;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return false;
    const bool ok = prefs.putString(TOKEN_KEY, token) == token.length() &&
                    prefs.putString(HANDLE_KEY, handle) == handle.length() &&
                    prefs.putString(DISPLAY_KEY, displayName) == displayName.length() &&
                    prefs.putBool(RECONNECT_KEY, false) == 1;
    prefs.end();
    return ok;
  }

  bool updateAccount(const String& handle, const String& displayName) {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return false;
    const bool ok = prefs.putString(HANDLE_KEY, handle) == handle.length() &&
                    prefs.putString(DISPLAY_KEY, displayName) == displayName.length();
    prefs.end();
    return ok;
  }

  void markReconnectRequired() {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    prefs.remove(TOKEN_KEY);
    prefs.putBool(RECONNECT_KEY, true);
    prefs.end();
  }

  void clear() {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return;
    prefs.clear();
    prefs.end();
  }
}  // namespace leaf_log_credentials
