#pragma once

namespace leaf_wifi {

void disableDiagnosticsUntilReboot();
bool diagnosticsAllowed();

void prepareForUserWifiSetup();
void prepareForUserWifiSetupFast();
void prepareForLeafAccessPoint();
void resetUserWifiSettings();
void attemptSavedNetworkConnection();
void disconnectFromNetwork();
bool savedNetworkConnectionInProgress();

}  // namespace leaf_wifi
