// Arduino HardwareSerial for the host emulator.
//
// Serial is the device's debug console: it goes to stdout and into the emulator's serial ring
// buffer so the browser panel can show it.  Serial0 is the GPS UART, wired to the virtual board's
// GPS pipe -- recorded NMEA played into a scenario is read back by the real LC86G driver, byte by
// byte, exactly as the receiver's own output would be.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "Stream.h"

class HardwareSerial : public Stream {
 public:
  virtual void begin(unsigned long baud = 115200) { (void)baud; }
  virtual void begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin) {
    (void)baud;
    (void)config;
    (void)rxPin;
    (void)txPin;
  }
  virtual void end() {}
  void setRxBufferSize(size_t) {}
  void setTxBufferSize(size_t) {}
  void updateBaudRate(unsigned long) {}
  void setDebugOutput(bool) {}
  operator bool() const { return true; }
};

class HostConsole : public HardwareSerial {
 public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  using Print::write;

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override;

  // The console is an append-only transcript, not a queue: the script's expect-serial assertions
  // and every connected browser read it through a cursor of their own, so no consumer can take a
  // line out from under another.  Appends the lines after `cursor` to `into` and returns the
  // cursor to pass next time.  A consumer that falls more than the retained history behind
  // silently resumes at the oldest line still held.
  static uint64_t linesSince(uint64_t cursor, std::vector<std::string>& into);

  // Sequence number one past the newest line, for a consumer that only wants what happens next.
  static uint64_t lineCount();
};

class HostGpsSerial : public HardwareSerial {
 public:
  size_t write(uint8_t c) override;
  using Print::write;

  int available() override;
  int read() override;
  int peek() override;
  void flush() override {}
};

#define SERIAL_8N1 0x800001c

extern HostConsole Serial;
extern HostGpsSerial Serial0;
extern HostConsole Serial1;
extern HostConsole Serial2;
