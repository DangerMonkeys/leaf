#include "comms/udp_message_server.h"

#include <Arduino.h>
#include <AsyncUDP.h>

#include "diagnostics/fatal_error.h"

AsyncUDP udp;
UDPMessageServer udpMessageServer;

void UDPMessageServer::init() {
  if (!udp.listen(7431)) {
    fatalError("Could not start async UDP server");
    return;
  }

  udp.onPacket([this](AsyncUDPPacket& packet) { this->onPacket(packet); });
}

void UDPMessageServer::onPacket(AsyncUDPPacket& packet) {
  if (!bus_) {
    Serial.println("Received UDP packet without a message bus");
    return;
  }
  if (packet.length() == 0) {
    Serial.printf("Empty UDP packet received from %s\n", packet.remoteIP());
    return;
  }
  const uint8_t* data = packet.data();
  char msgType = static_cast<char>(data[0]);
  const char* line = reinterpret_cast<const char*>(data);
  size_t n = packet.length();
  if (msgType == '#') {
    onComment(line, n);
  } else if (msgType == '!') {
    onCommand(line, n);
  } else if (msgType == 'A') {
    onAmbientUpdate(line, n);
  } else if (msgType == 'G') {
    onGPSMessage(line, n);
  } else if (msgType == 'M') {
    onMotionUpdate(line, n);
  } else if (msgType = 'P') {
    onPressureUpdate(line, n);
  } else {
    Serial.printf("Unrecognized UDP message type: '%s'\n", msgType);
  }
}

namespace {
  bool equals_const(const char* buf, size_t len, const char* literal) {
    size_t litLen = strlen(literal);
    return (len == litLen) && (memcmp(buf, literal, litLen) == 0);
  }
}  // namespace

void UDPMessageServer::onComment(const char* line, size_t len) {
  Serial.print("Comment via UDP: ");
  Serial.write(line + 1, len - 1);
  Serial.println();
}

void UDPMessageServer::onCommand(const char* line, size_t len) {
  if (equals_const(line, len, "!Disable sensors")) {
    Serial.println("Disable sensors via UDP");
  } else if (equals_const(line, len, "!Reset reference time")) {
    tStart_ = 0;
    Serial.println("UDP server reference time reset");
  } else {
    Serial.print("Unrecognized UDP command: ");
    Serial.write(line, len);
    Serial.println();
  }
}

void UDPMessageServer::onAmbientUpdate(const char* line, size_t len) {
  Serial.println("UDP Ambient");
}

void UDPMessageServer::onGPSMessage(const char* line, size_t len) { Serial.println("UDP GPS"); }

void UDPMessageServer::onMotionUpdate(const char* line, size_t len) {
  Serial.println("UDP Motion");
}

void UDPMessageServer::onPressureUpdate(const char* line, size_t len) {
  Serial.println("UDP Pressure");
}
