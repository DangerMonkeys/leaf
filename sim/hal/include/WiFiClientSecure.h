// TLS client stand-in.
//
// The emulator has no network path out (see HTTPClient.h), so this only needs to exist as a type
// HTTPClient::begin can accept.
#pragma once

class WiFiClientSecure {
 public:
  void setCACert(const char*) {}
};
