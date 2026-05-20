#pragma once

#include <Arduino.h>
#include <AsyncUDP.h>

class FactoryDiscovery {
 public:
  void update();
  String statusJson() const;

 private:
  void start();
  void stop();
  void onPacket(AsyncUDPPacket& packet);

  AsyncUDP udp_;
  bool listening_ = false;
  bool last_listen_attempt_failed_ = false;
  uint32_t last_listen_attempt_ms_ = 0;
  uint32_t last_listen_success_ms_ = 0;
  uint32_t last_listen_failure_ms_ = 0;
  uint32_t last_stop_ms_ = 0;
  uint32_t last_packet_ms_ = 0;
  uint32_t last_response_ms_ = 0;
  uint32_t packets_received_ = 0;
  uint32_t responses_sent_ = 0;
  uint32_t ignored_not_off_usb_ = 0;
  uint32_t ignored_not_diagnostic_wifi_ = 0;
  uint32_t ignored_short_packet_ = 0;
  uint32_t ignored_bad_prefix_ = 0;
  uint32_t ignored_nonce_too_long_ = 0;
};

extern FactoryDiscovery factoryDiscovery;
