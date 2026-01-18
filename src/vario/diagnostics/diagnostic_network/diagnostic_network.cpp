#include "diagnostic_network.h"

#include <WiFi.h>

#include "diagnostics/fatal_error.h"
#include "power.h"

static constexpr const char* DIAGNOSTIC_NETWORK_SSID = "LeafDiagnostics";
static constexpr const char* DIAGNOSTIC_NETWORK_PASSWORD = "leafdiagnostics";

static constexpr int32_t MIN_RSSI_DBM = -85;  // ignore super-weak signals
static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;

DiagnosticNetwork diagnostic_network;

void DiagnosticNetwork::update() {
  if (state_ == State::Ready) {
    maybeLookForNetwork();
  } else if (state_ == State::LookingForNetwork) {
    checkForDiagnosticNetwork();
  } else if (state_ == State::NoNetworkFound) {
    // Do nothing
    if (!printed_end_state_) {
      Serial.println("DiagnosticNetwork: No network found");
      printed_end_state_ = true;
    }
  } else if (state_ == State::ConnectingToNetwork) {
    checkForConnection();
  } else if (state_ == State::ConnectedToNetwork) {
    // Do nothing
    if (!printed_end_state_) {
      Serial.println("DiagnosticNetwork: Connected to network");
      printed_end_state_ = true;
    }
  } else if (state_ == State::Error) {
    // Do nothing
    if (!printed_end_state_) {
      Serial.print("DiagnosticNetwork: Error: ");
      Serial.println(error_msg_);
      printed_end_state_ = true;
    }
  } else {
    fatalError("Unsupported state %d", (uint8_t)state_);
  }
}

void DiagnosticNetwork::maybeLookForNetwork() {
  const Power::Info& info = power.info();

  // Only look for the network if we're charging
  if (info.charging) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);  // drop old state, clear config
    delay(100);

    // Clean up any previous scan results
    WiFi.scanDelete();

    // Start scanning
    int16_t result = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    if (result == WIFI_SCAN_RUNNING) {
      state_ = State::LookingForNetwork;
      return;
    } else {
      error_msg_ = "WiFi.scanNetworks did not indicate WIFI_SCAN_RUNNING";
      state_ = State::Error;
      return;
    }
  }
}

void DiagnosticNetwork::checkForDiagnosticNetwork() {
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    return;
  } else if (result == WIFI_SCAN_FAILED) {
    error_msg_ = "WiFi.scanComplete indicated WIFI_SCAN_FAILED";
    state_ = State::Error;
    Serial.println("DiagnosticNetwork: WiFi.scanComplete failed");
    return;
  } else if (result < 0) {
    Serial.printf("DiagnosticNetwork: WiFi.scanComplete unexpected result %d\n", result);
    state_ = State::Error;
    return;
  }

  int32_t bestRssi = -127;
  bool found = false;
  Serial.printf("DiagnosticNetwork: scanComplete found %d networks\n", result);

  for (int i = 0; i < result; i++) {
    String s = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);

    if (s == DIAGNOSTIC_NETWORK_SSID) {
      found = true;
      if (rssi > bestRssi) bestRssi = rssi;  // if multiple APs share SSID
    }
  }

  WiFi.scanDelete();

  if (found && bestRssi > MIN_RSSI_DBM) {
    t0_ = millis();
    WiFi.begin(DIAGNOSTIC_NETWORK_SSID, DIAGNOSTIC_NETWORK_PASSWORD);
    state_ = State::ConnectingToNetwork;
    return;
  } else {
    Serial.printf(
        "checkForDiagnosticNetwork -> %d with RSSI %d (min %d) scan count %d\n",
        found,
        bestRssi,
        MIN_RSSI_DBM,
        result);
    state_ = State::NoNetworkFound;
    return;
  }
}

void DiagnosticNetwork::checkForConnection() {
  uint32_t now = millis();
  if (now - t0_ > CONNECT_TIMEOUT_MS) {
    WiFi.disconnect(true, true);
    error_msg_ = "Timeout attempting to connect to diagnostic network";
    state_ = State::Error;
    return;
  }

  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    state_ = State::ConnectedToNetwork;
    return;
  } else if (status == WL_CONNECT_FAILED || status == WL_STOPPED) {
    WiFi.disconnect(true, true);
    error_msg_ = "WiFi FAILED or STOPPED while connecting to diagnostic network";
    state_ = State::Error;
  }
}
