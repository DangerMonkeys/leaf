#include "comms/factory_discovery.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

#include "diagnostics/diagnostic_network/diagnostic_network.h"
#include "power.h"

namespace {
  constexpr uint16_t DISCOVERY_PORT = 7432;
  constexpr uint16_t HTTP_PORT = 81;
  constexpr const char* REQUEST_PREFIX = "LEAF_DISCOVERY_REQUEST/1 ";
  constexpr size_t REQUEST_PREFIX_LEN = 25;
  constexpr const char* DIAGNOSTIC_NETWORK_SSID = "LeafDiagnostics";

  bool connectedToDiagnosticWifi() {
    return WiFi.status() == WL_CONNECTED && WiFi.SSID() == DIAGNOSTIC_NETWORK_SSID;
  }

  bool discoveryEligible() {
    return connectedToDiagnosticWifi() && power.info().onState == PowerState::OffUSB;
  }

  const char* wifiStatusName(wl_status_t status) {
    switch (status) {
      case WL_IDLE_STATUS:
        return "idle";
      case WL_NO_SSID_AVAIL:
        return "no_ssid_available";
      case WL_SCAN_COMPLETED:
        return "scan_completed";
      case WL_CONNECTED:
        return "connected";
      case WL_CONNECT_FAILED:
        return "connect_failed";
      case WL_CONNECTION_LOST:
        return "connection_lost";
      case WL_DISCONNECTED:
        return "disconnected";
      default:
        return "unknown";
    }
  }

  const char* diagnosticNetworkStateName(DiagnosticNetwork::State state) {
    switch (state) {
      case DiagnosticNetwork::State::Ready:
        return "ready";
      case DiagnosticNetwork::State::WifiResetting:
        return "wifi_resetting";
      case DiagnosticNetwork::State::LookingForNetwork:
        return "looking_for_network";
      case DiagnosticNetwork::State::NoNetworkFound:
        return "no_network_found";
      case DiagnosticNetwork::State::ConnectingToNetwork:
        return "connecting_to_network";
      case DiagnosticNetwork::State::ConnectedToNetwork:
        return "connected_to_network";
      case DiagnosticNetwork::State::Error:
        return "error";
      default:
        return "unknown";
    }
  }

  const char* powerStateName(PowerState state) {
    switch (state) {
      case PowerState::Off:
        return "off";
      case PowerState::On:
        return "on";
      case PowerState::OffUSB:
        return "off_usb";
      default:
        return "unknown";
    }
  }

  void appendJsonString(String& json, const String& value) {
    json += "\"";
    for (size_t i = 0; i < value.length(); i++) {
      const char c = value[i];
      if (c == '"' || c == '\\') json += "\\";
      json += c;
    }
    json += "\"";
  }
}  // namespace

FactoryDiscovery factoryDiscovery;

void FactoryDiscovery::update() {
  if (discoveryEligible()) {
    if (!listening_) start();
  } else if (listening_) {
    stop();
  }
}

void FactoryDiscovery::start() {
  last_listen_attempt_ms_ = millis();
  if (!udp_.listen(DISCOVERY_PORT)) {
    last_listen_attempt_failed_ = true;
    last_listen_failure_ms_ = millis();
    Serial.println("FactoryDiscovery: failed to listen");
    return;
  }

  udp_.onPacket([this](AsyncUDPPacket& packet) { this->onPacket(packet); });
  listening_ = true;
  last_listen_attempt_failed_ = false;
  last_listen_success_ms_ = millis();
  Serial.printf("FactoryDiscovery: listening on UDP %u\n", DISCOVERY_PORT);
}

void FactoryDiscovery::stop() {
  udp_.close();
  listening_ = false;
  last_stop_ms_ = millis();
  Serial.println("FactoryDiscovery: stopped");
}

void FactoryDiscovery::onPacket(AsyncUDPPacket& packet) {
  packets_received_++;
  last_packet_ms_ = millis();

  if (power.info().onState != PowerState::OffUSB) {
    ignored_not_off_usb_++;
    return;
  }
  if (!connectedToDiagnosticWifi()) {
    ignored_not_diagnostic_wifi_++;
    return;
  }
  if (packet.length() <= REQUEST_PREFIX_LEN) {
    ignored_short_packet_++;
    return;
  }

  const char* data = reinterpret_cast<const char*>(packet.data());
  if (std::memcmp(data, REQUEST_PREFIX, REQUEST_PREFIX_LEN) != 0) {
    ignored_bad_prefix_++;
    return;
  }

  const size_t nonce_len = packet.length() - REQUEST_PREFIX_LEN;
  if (nonce_len > 64) {
    ignored_nonce_too_long_++;
    return;
  }

  char nonce[65];
  std::memcpy(nonce, data + REQUEST_PREFIX_LEN, nonce_len);
  nonce[nonce_len] = '\0';

  String mac = WiFi.macAddress();
  char response[256];
  snprintf(response, sizeof(response),
           "{\"type\":\"leaf_discovery_response\",\"version\":1,"
           "\"nonce\":\"%s\",\"device_id\":\"%s\",\"mac_address\":\"%s\",\"http_port\":%u}",
           nonce, mac.c_str(), mac.c_str(), HTTP_PORT);

  packet.print(response);
  responses_sent_++;
  last_response_ms_ = millis();
  Serial.printf("FactoryDiscovery: responded to %s\n", packet.remoteIP().toString().c_str());
}

String FactoryDiscovery::statusJson() const {
  const wl_status_t status = WiFi.status();
  const String ssid = WiFi.SSID();
  const bool connected = status == WL_CONNECTED;
  const bool connected_to_diagnostic_wifi = connectedToDiagnosticWifi();
  const Power::Info& power_info = power.info();
  const bool off_usb = power_info.onState == PowerState::OffUSB;
  const bool diagnostic_network_connected = diagnostic_network.connected();
  const bool should_listen = connected_to_diagnostic_wifi && off_usb;
  const bool should_respond = listening_ && connected_to_diagnostic_wifi && off_usb;

  String json = "{";
  json += "\"discovery\":{";
  json += "\"listening\":";
  json += listening_ ? "true" : "false";
  json += ",\"should_listen\":";
  json += should_listen ? "true" : "false";
  json += ",\"should_respond\":";
  json += should_respond ? "true" : "false";
  json += ",\"port\":";
  json += DISCOVERY_PORT;
  json += ",\"http_port\":";
  json += HTTP_PORT;
  json += ",\"request_prefix\":";
  appendJsonString(json, REQUEST_PREFIX);
  json += ",\"last_listen_attempt_failed\":";
  json += last_listen_attempt_failed_ ? "true" : "false";
  json += ",\"last_listen_attempt_ms\":";
  json += last_listen_attempt_ms_;
  json += ",\"last_listen_success_ms\":";
  json += last_listen_success_ms_;
  json += ",\"last_listen_failure_ms\":";
  json += last_listen_failure_ms_;
  json += ",\"last_stop_ms\":";
  json += last_stop_ms_;
  json += ",\"last_packet_ms\":";
  json += last_packet_ms_;
  json += ",\"last_response_ms\":";
  json += last_response_ms_;
  json += ",\"packets_received\":";
  json += packets_received_;
  json += ",\"responses_sent\":";
  json += responses_sent_;
  json += ",\"ignored_not_off_usb\":";
  json += ignored_not_off_usb_;
  json += ",\"ignored_not_diagnostic_wifi\":";
  json += ignored_not_diagnostic_wifi_;
  json += ",\"ignored_short_packet\":";
  json += ignored_short_packet_;
  json += ",\"ignored_bad_prefix\":";
  json += ignored_bad_prefix_;
  json += ",\"ignored_nonce_too_long\":";
  json += ignored_nonce_too_long_;
  json += "},\"wifi\":{";
  json += "\"status_code\":";
  json += static_cast<int>(status);
  json += ",\"status\":";
  appendJsonString(json, wifiStatusName(status));
  json += ",\"connected\":";
  json += connected ? "true" : "false";
  json += ",\"ssid\":";
  appendJsonString(json, ssid);
  json += ",\"expected_ssid\":";
  appendJsonString(json, DIAGNOSTIC_NETWORK_SSID);
  json += ",\"connected_to_diagnostic_wifi\":";
  json += connected_to_diagnostic_wifi ? "true" : "false";
  json += ",\"local_ip\":";
  appendJsonString(json, WiFi.localIP().toString());
  json += ",\"mac_address\":";
  appendJsonString(json, WiFi.macAddress());
  json += ",\"mode\":";
  json += static_cast<int>(WiFi.getMode());
  json += ",\"rssi\":";
  json += WiFi.RSSI();
  json += "},\"diagnostic_network\":{";
  json += "\"state\":";
  appendJsonString(json, diagnosticNetworkStateName(diagnostic_network.state()));
  json += ",\"connected\":";
  json += diagnostic_network_connected ? "true" : "false";
  json += ",\"error\":";
  appendJsonString(json, diagnostic_network.error_msg());
  json += "},\"power\":{";
  json += "\"state\":";
  appendJsonString(json, powerStateName(power_info.onState));
  json += ",\"off_usb\":";
  json += off_usb ? "true" : "false";
  json += ",\"usb_input\":";
  json += power_info.USBinput ? "true" : "false";
  json += ",\"charging\":";
  json += power_info.charging ? "true" : "false";
  json += "},\"reason\":";
  if (!connected) {
    appendJsonString(json, "WiFi is not connected.");
  } else if (!connected_to_diagnostic_wifi) {
    appendJsonString(json, "WiFi is connected, but not to the LeafDiagnostics SSID.");
  } else if (!off_usb) {
    appendJsonString(json, "Device is on LeafDiagnostics, but is not in OffUSB.");
  } else if (!listening_) {
    appendJsonString(json, "Device is on LeafDiagnostics, but discovery UDP is not listening.");
  } else {
    appendJsonString(json, "Device should respond to valid discovery pings.");
  }
  json += "}";
  return json;
}
