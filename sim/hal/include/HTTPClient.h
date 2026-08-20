// HTTP client stand-in.
//
// The emulator has no real network path out, so begin() always fails -- the same "report
// yourself unavailable" contract WiFi.h and the other network stand-ins use, rather than
// pretending a request could ever succeed.
#pragma once

#include <stddef.h>

#include "Stream.h"
#include "WString.h"

class WiFiClientSecure;

class HTTPClient {
 public:
  bool begin(WiFiClientSecure&, const String&) { return false; }
  bool begin(const String&) { return false; }
  void setConnectTimeout(int) {}
  void setTimeout(int) {}
  void addHeader(const String&, const String&) {}
  int sendRequest(const char*, Stream* = nullptr, size_t = 0) { return -1; }
  int GET() { return -1; }
  String getString() { return String(); }
  void end() {}
};
