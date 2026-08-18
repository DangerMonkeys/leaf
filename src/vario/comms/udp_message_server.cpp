#include "comms/udp_message_server.h"

#include <Arduino.h>
#include <AsyncUDP.h>

#include "diagnostics/fatal_error.h"

AsyncUDP udp;
UDPMessageServer udpMessageServer;

const uint16_t UDP_PORT = 7431;

void UDPMessageServer::init() {
  if (!udp.listen(UDP_PORT)) {
    fatalError("Could not start async UDP server");
    return;
  }

  udp.onPacket([this](AsyncUDPPacket& packet) { this->onPacket(packet); });
}

void UDPMessageServer::onPacket(AsyncUDPPacket& packet) {
  if (!injector_.bus()) {
    Serial.println("Received UDP packet without a message bus");
    return;
  }
  if (packet.length() == 0) {
    Serial.println("Empty UDP packet received");
    return;
  }

  const char* line = reinterpret_cast<const char*>(packet.data());
  if (!injector_.handleLine(line, packet.length())) {
    Serial.printf("Unrecognized UDP message type: '%c'\n", line[0]);
  }
}
