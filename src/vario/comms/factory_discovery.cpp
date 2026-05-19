#include "comms/factory_discovery.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

#include "diagnostics/diagnostic_network/diagnostic_network.h"

namespace {
  constexpr uint16_t DISCOVERY_PORT = 7432;
  constexpr uint16_t HTTP_PORT = 81;
  constexpr const char* REQUEST_PREFIX = "LEAF_DISCOVERY_REQUEST/1 ";
  constexpr size_t REQUEST_PREFIX_LEN = 25;
}  // namespace

FactoryDiscovery factoryDiscovery;

void FactoryDiscovery::update() {
  if (diagnostic_network.connected()) {
    if (!listening_) start();
  } else if (listening_) {
    stop();
  }
}

void FactoryDiscovery::start() {
  if (!udp_.listen(DISCOVERY_PORT)) {
    Serial.println("FactoryDiscovery: failed to listen");
    return;
  }

  udp_.onPacket([this](AsyncUDPPacket& packet) { this->onPacket(packet); });
  listening_ = true;
  Serial.printf("FactoryDiscovery: listening on UDP %u\n", DISCOVERY_PORT);
}

void FactoryDiscovery::stop() {
  udp_.close();
  listening_ = false;
  Serial.println("FactoryDiscovery: stopped");
}

void FactoryDiscovery::onPacket(AsyncUDPPacket& packet) {
  if (!diagnostic_network.connected()) return;
  if (packet.length() <= REQUEST_PREFIX_LEN) return;

  const char* data = reinterpret_cast<const char*>(packet.data());
  if (std::memcmp(data, REQUEST_PREFIX, REQUEST_PREFIX_LEN) != 0) return;

  const size_t nonce_len = packet.length() - REQUEST_PREFIX_LEN;
  if (nonce_len > 64) return;

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
  Serial.printf("FactoryDiscovery: responded to %s\n", packet.remoteIP().toString().c_str());
}
