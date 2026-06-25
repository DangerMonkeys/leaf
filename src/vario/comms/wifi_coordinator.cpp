#include "comms/wifi_coordinator.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

namespace leaf_wifi {
namespace {
  bool diagnostics_disabled_until_reboot = false;
  bool saved_network_connecting = false;
  uint32_t saved_network_connect_started_ms = 0;
  constexpr uint32_t WIFI_SETTLE_MS = 150;
  constexpr uint32_t WIFI_HARD_RESET_SETTLE_MS = 500;
  constexpr uint32_t SAVED_NETWORK_CONNECT_DISPLAY_MS = 6500;

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

  void clearSavedNetworkAttempt() {
    saved_network_connecting = false;
    saved_network_connect_started_ms = 0;
  }

  void updateSavedNetworkAttempt() {
    if (!saved_network_connecting) return;
    if (WiFi.status() == WL_CONNECTED ||
        millis() - saved_network_connect_started_ms > SAVED_NETWORK_CONNECT_DISPLAY_MS) {
      clearSavedNetworkAttempt();
    }
  }

  bool hasSavedStationConfig() {
    wifi_config_t config = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK) return false;
    return config.sta.ssid[0] != '\0';
  }

  void stopScansAndSta(bool eraseCredentials) {
    clearSavedNetworkAttempt();
    WiFi.scanDelete();
    WiFi.disconnect(/*wifioff=*/false, eraseCredentials);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    settleWifi();
    WiFi.scanDelete();
  }

  void resetRadioForSetup(bool eraseCredentials) {
    clearSavedNetworkAttempt();
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

void prepareForUserWifiSetupFast() {
  disableDiagnostics();
  clearSavedNetworkAttempt();
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
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
  if (!hasSavedStationConfig()) {
    clearSavedNetworkAttempt();
    return;
  }
  WiFi.begin();
  saved_network_connecting = true;
  saved_network_connect_started_ms = millis();
}

void disconnectFromNetwork() {
  clearSavedNetworkAttempt();
  WiFi.scanDelete();
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
}

bool savedNetworkConnectionInProgress() {
  updateSavedNetworkAttempt();
  return saved_network_connecting;
}

}  // namespace leaf_wifi
