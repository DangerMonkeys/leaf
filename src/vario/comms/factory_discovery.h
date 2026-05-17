#pragma once

#include <AsyncUDP.h>

class FactoryDiscovery {
 public:
  void update();

 private:
  void start();
  void stop();
  void onPacket(AsyncUDPPacket& packet);

  AsyncUDP udp_;
  bool listening_ = false;
};

extern FactoryDiscovery factoryDiscovery;
