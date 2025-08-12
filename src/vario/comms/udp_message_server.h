#pragma once

#include <AsyncUDP.h>
#include "etl/message_bus.h"

#include "dispatch/message_source.h"

class UDPMessageServer : public IMessageSource {
 public:
  void init();

  // IMessageSource
  void publishTo(etl::imessage_bus* bus) { bus_ = bus; }
  void stopPublishing() { bus_ = nullptr; }

 private:
  void onPacket(AsyncUDPPacket& packet);

  unsigned long getAdjustedTime(unsigned long dt);

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
