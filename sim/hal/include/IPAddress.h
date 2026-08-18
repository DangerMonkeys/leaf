#pragma once

#include <stdint.h>

#include "Print.h"
#include "WString.h"

class IPAddress : public Printable {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a, b, c, d} {}
  explicit IPAddress(uint32_t address) {
    bytes_[0] = address & 0xFF;
    bytes_[1] = (address >> 8) & 0xFF;
    bytes_[2] = (address >> 16) & 0xFF;
    bytes_[3] = (address >> 24) & 0xFF;
  }

  String toString() const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", bytes_[0], bytes_[1], bytes_[2], bytes_[3]);
    return String(buf);
  }
  size_t printTo(Print& p) const override { return p.print(toString()); }
  uint8_t operator[](int index) const { return bytes_[index & 3]; }
  operator uint32_t() const {
    return (uint32_t)bytes_[0] | ((uint32_t)bytes_[1] << 8) | ((uint32_t)bytes_[2] << 16) |
           ((uint32_t)bytes_[3] << 24);
  }
  bool operator==(const IPAddress& other) const { return memcmp(bytes_, other.bytes_, 4) == 0; }

 private:
  uint8_t bytes_[4] = {0, 0, 0, 0};
};

#define INADDR_NONE IPAddress(0, 0, 0, 0)
