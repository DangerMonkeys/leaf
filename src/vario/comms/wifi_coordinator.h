#pragma once

namespace leaf_wifi {

void disableDiagnosticsUntilReboot();
bool diagnosticsAllowed();

void prepareForUserWifiSetup();
void prepareForLeafAccessPoint();
void resetUserWifiSettings();
void attemptSavedNetworkConnection();
void disconnectFromNetwork();

}  // namespace leaf_wifi
