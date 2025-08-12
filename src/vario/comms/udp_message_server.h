#pragma once

#include <AsyncUDP.h>
#include "etl/message_bus.h"

#include "dispatch/message_source.h"

class UDPMessageServer : public IMessageSource {
 public:
  void init();

  // IMessageSource
  void attach(etl::imessage_bus* bus) { bus_ = bus; }

 private:
  void onPacket(AsyncUDPPacket& packet);

  void onComment(const char* line, size_t len);
  void onCommand(const char* line, size_t len);
  void onAmbientUpdate(const char* line, size_t len);
  void onGPSMessage(const char* line, size_t len);
  void onMotionUpdate(const char* line, size_t len);
  void onPressureUpdate(const char* line, size_t len);

  etl::imessage_bus* bus_ = nullptr;
  unsigned long tStart_ = 0;
};

extern UDPMessageServer udpMessageServer;
