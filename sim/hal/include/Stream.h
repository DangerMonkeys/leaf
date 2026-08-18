// Arduino Stream for the host emulator.
#pragma once

#include "Print.h"

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;

  void setTimeout(unsigned long timeout) { timeout_ = timeout; }
  unsigned long getTimeout() const { return timeout_; }

  virtual size_t readBytes(char* buffer, size_t length) {
    size_t count = 0;
    while (count < length) {
      const int c = read();
      if (c < 0) break;
      buffer[count++] = (char)c;
    }
    return count;
  }
  size_t readBytes(uint8_t* buffer, size_t length) { return readBytes((char*)buffer, length); }

  String readString() {
    String out;
    int c;
    while ((c = read()) >= 0) out += (char)c;
    return out;
  }
  String readStringUntil(char terminator) {
    String out;
    int c;
    while ((c = read()) >= 0 && (char)c != terminator) out += (char)c;
    return out;
  }
  bool find(const char*) { return false; }

 protected:
  unsigned long timeout_ = 1000;
};
