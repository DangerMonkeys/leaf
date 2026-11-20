#include "comms/udp_message_server.h"

#include <Arduino.h>
#include <AsyncUDP.h>
#include <WiFi.h>

#include "power.h"
#include "ui/settings/settings.h"

namespace {
  AsyncUDP udp;
  constexpr uint16_t UDP_PORT = 7431;

  bool injectionEnabled() {
    if (!settings.udpInjectionEnabled()) return false;
#ifdef LEAF_SIM
    // The emulator has a real host socket and no physical WiFi connection.
    return true;
#else
    const auto mode = WiFi.getMode();
    return WiFi.status() == WL_CONNECTED || mode == WIFI_AP || mode == WIFI_AP_STA;
#endif
  }
}  // namespace

UDPMessageServer udpMessageServer;

UDPMessageServer::UDPMessageServer() {
  udp.onPacket([this](AsyncUDPPacket& packet) { this->onPacket(packet); });
}

UDPMessageServer::~UDPMessageServer() {
  accepting_.store(false);
  udp.close();
}

void UDPMessageServer::update() {
  if (power.info().onState != PowerState::On || !injectionEnabled()) {
    callbacks_.requestStop();
    if (accepting_.exchange(false)) {
      resetReference_.store(true);
      Serial.println("UDP injection stopped");
    }
    attemptedListen_ = false;
    return;
  }

  // Never reopen over a callback left from a previous enable/disable cycle.
  if (!callbacks_.idle()) return;
  callbacks_.reopen();

  // ESP32 AsyncUDP::close disconnects but does not unbind its PCB or clear queued callbacks.
  // Keep one listener and gate packet handling instead of repeatedly closing/rebinding it.
  if (listening_) {
    if (!accepting_.exchange(true)) Serial.println("UDP injection enabled");
    return;
  }
  if (attemptedListen_ && millis() - lastListenAttemptMs_ < 1000) return;
  attemptedListen_ = true;
  lastListenAttemptMs_ = millis();
  listening_ = udp.listen(UDP_PORT);
  accepting_.store(listening_);
  Serial.println(listening_ ? "UDP injection listening on port 7431"
                            : "Could not start UDP injection; will retry");
}

bool UDPMessageServer::quiesce(uint32_t timeoutMs) {
  accepting_.store(false);
  callbacks_.requestStop();
  resetReference_.store(true);
  const uint32_t started = millis();
  while (!callbacks_.idle()) {
    if (millis() - started >= timeoutMs) return false;
    delay(1);
  }
  return true;
}

void UDPMessageServer::onPacket(AsyncUDPPacket& packet) {
  // Settings and WiFi are inspected on the main loop; the callback only reads atomic state.
  ProducerGate::Entry callback(callbacks_);
  if (!callback || !accepting_.load()) return;
  if (resetReference_.exchange(false)) injector_.resetReferenceTime();
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
