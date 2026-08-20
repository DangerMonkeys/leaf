#pragma once

#include <AsyncUDP.h>
#include "etl/message_bus.h"

#include "dispatch/message_injector.h"
#include "dispatch/message_source.h"

// Receives injected sensor messages over UDP (see sim/play_buslog.py).  The wire format and its
// parsing live in MessageInjector, shared with the device emulator.
class UDPMessageServer : public IMessageSource {
 public:
  void init();

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { injector_.setBus(bus); }
  void stopPublishing() { injector_.setBus(nullptr); }

 private:
  void onPacket(AsyncUDPPacket& packet);

  MessageInjector injector_;
};

extern UDPMessageServer udpMessageServer;
