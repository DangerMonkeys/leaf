// AsyncUDP over a real host socket.
//
// This one is not a stub: the firmware's UDP injection server (comms/udp_message_server.cpp) is
// compiled into the emulator and listens on a real port, so sim/play_buslog.py -- the same script
// used against hardware -- can drive the emulated device.
#pragma once

#include <stdint.h>

#include <functional>
#include <string>

#include "IPAddress.h"

class AsyncUDPPacket {
 public:
  AsyncUDPPacket(uint8_t* data, size_t length, IPAddress remote)
      : data_(data), length_(length), remote_(remote) {}

  uint8_t* data() const { return data_; }
  size_t length() const { return length_; }
  IPAddress remoteIP() const { return remote_; }
  uint16_t remotePort() const { return 0; }

 private:
  uint8_t* data_;
  size_t length_;
  IPAddress remote_;
};

class AsyncUDP {
 public:
  ~AsyncUDP();
  bool listen(uint16_t port);
  void close();
  void onPacket(std::function<void(AsyncUDPPacket&)> callback);

 private:
  int socket_ = -1;
  std::function<void(AsyncUDPPacket&)> callback_;
  bool running_ = false;
};
