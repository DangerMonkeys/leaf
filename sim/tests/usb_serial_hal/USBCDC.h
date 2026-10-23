#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Only the transport is replaced: usb_serial.cpp is compiled unchanged for this test.
struct TestTxLock {
  bool held = false;
};
using SemaphoreHandle_t = TestTxLock*;
constexpr int pdPASS = 1;
int xSemaphoreTake(SemaphoreHandle_t, unsigned ticks);
void xSemaphoreGive(SemaphoreHandle_t);
bool xPortInIsrContext();

class USBCDC {
 public:
  explicit USBCDC(uint8_t interface = 0) : itf(interface) {}
  virtual ~USBCDC() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  size_t write(const char* text) {
    return write(reinterpret_cast<const uint8_t*>(text), strlen(text));
  }
  virtual void flush() = 0;
  void begin(unsigned long = 0) { tx_lock = &testLock; }
  TestTxLock testLock;

 protected:
  uint8_t itf;
  SemaphoreHandle_t tx_lock = nullptr;
};
