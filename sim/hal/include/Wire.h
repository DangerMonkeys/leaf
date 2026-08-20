// I2C stand-in.  The emulator injects sensor data as bus messages rather than as register reads,
// so nothing is on this bus: transactions succeed and read back zeros.  It exists because the
// real drivers and the vendored ICM-20948 library are still compiled.
#pragma once

#include <stdint.h>

#include "Stream.h"

class TwoWire : public Stream {
 public:
  explicit TwoWire(uint8_t busNum = 0) : bus_(busNum) {}

  bool begin() { return true; }
  bool begin(int sda, int scl, uint32_t frequency = 0) { return true; }
  bool end() { return true; }
  void setClock(uint32_t frequency) { (void)frequency; }
  void setTimeOut(uint16_t ms) { (void)ms; }
  uint16_t getTimeOut() { return 50; }

  void beginTransmission(uint8_t address) { address_ = address; }
  void beginTransmission(int address) { address_ = (uint8_t)address; }
  uint8_t endTransmission(bool sendStop = true) { return 2; }  // 2 = NACK on address: no device
  uint8_t requestFrom(uint8_t address, size_t size, bool sendStop = true) { return 0; }
  uint8_t requestFrom(int address, int size) { return 0; }

  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t size) override { return size; }
  using Print::write;

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  uint8_t bus_ = 0;
  uint8_t address_ = 0;
};

extern TwoWire Wire;
extern TwoWire Wire1;
