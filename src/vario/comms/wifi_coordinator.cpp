#include "comms/wifi_coordinator.h"

#include <Arduino.h>
#include <WiFi.h>

namespace leaf_wifi {
namespace {
  bool diagnostics_disabled_until_reboot = false;
  constexpr uint32_t WIFI_SETTLE_MS = 150;
  constexpr uint32_t WIFI_HARD_RESET_SETTLE_MS = 500;

  void settleWifi() {
    delay(WIFI_SETTLE_MS);
  }

  void hardSettleWifi() {
    delay(WIFI_HARD_RESET_SETTLE_MS);
  }

  void disableDiagnostics() {
    if (!diagnostics_disabled_until_reboot) {
      Serial.println("WiFi coordinator: diagnostic network disabled until reboot");
    }
    diagnostics_disabled_until_reboot = true;
  }

  void stopScansAndSta(bool eraseCredentials) {
    WiFi.scanDelete();
    WiFi.disconnect(/*wifioff=*/false, eraseCredentials);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    settleWifi();
    WiFi.scanDelete();
  }

  void resetRadioForSetup(bool eraseCredentials) {
    WiFi.scanDelete();
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(/*wifioff=*/false, eraseCredentials);
    WiFi.mode(WIFI_OFF);
    hardSettleWifi();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    hardSettleWifi();
    WiFi.scanDelete();
  }
}  // namespace

void disableDiagnosticsUntilReboot() {
  disableDiagnostics();
}

bool diagnosticsAllowed() {
  return !diagnostics_disabled_until_reboot;
}

void prepareForUserWifiSetup() {
  disableDiagnostics();
  resetRadioForSetup(/*eraseCredentials=*/false);
}

void prepareForLeafAccessPoint() {
  disableDiagnostics();
  stopScansAndSta(/*eraseCredentials=*/false);
}

void resetUserWifiSettings() {
  disableDiagnostics();
  resetRadioForSetup(/*eraseCredentials=*/true);
  Serial.println("WiFi settings reset");
}

void attemptSavedNetworkConnection() {
  disableDiagnostics();
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin();
}

void disconnectFromNetwork() {
  WiFi.scanDelete();
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
}

}  // namespace leaf_wifi
