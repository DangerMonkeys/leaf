#pragma once

#include <AsyncUDP.h>
#include <atomic>
#include "etl/message_bus.h"

#include "dispatch/message_injector.h"
#include "dispatch/message_source.h"

// Receives injected sensor messages over UDP (see sim/play_buslog.py).  The wire format and its
// parsing live in MessageInjector, shared with the device emulator.
class UDPMessageServer : public IMessageSource {
 public:
  UDPMessageServer();
  ~UDPMessageServer() override;

  // Require both developer mode and its injection opt-in, plus a network on hardware.
  void update();

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { injector_.setBus(bus); }
  void stopPublishing() { injector_.setBus(nullptr); }

 private:
  void onPacket(AsyncUDPPacket& packet);

  MessageInjector injector_;
  std::atomic<bool> accepting_{false};
  std::atomic<bool> resetReference_{true};
  bool listening_ = false;
  bool attemptedListen_ = false;
  uint32_t lastListenAttemptMs_ = 0;
};

extern UDPMessageServer udpMessageServer;
