#pragma once

#include <Arduino.h>

namespace leaf_wifi {

  void disableDiagnosticsUntilReboot();
  bool diagnosticsAllowed();

  void prepareForUserWifiSetup();
  void prepareForUserWifiSetupFast();
  void prepareForLeafAccessPoint();
  void resetUserWifiSettings();
  void clearSavedNetworkCredentials();
  void rememberSuccessfulNetwork(const String& ssid, const String& password);
  void attemptSavedNetworkConnection();
  void disconnectFromNetwork();
  bool savedNetworkConnectionInProgress();

}  // namespace leaf_wifi
