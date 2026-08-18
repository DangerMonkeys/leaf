#pragma once
#include <stdint.h>
// USB mass storage has no host equivalent; the emulated card is a folder you can open directly.
class USBMSC {
 public:
  bool begin(uint32_t blockCount, uint16_t blockSize) { return false; }
  void end() {}
  void vendorID(const char*) {}
  void productID(const char*) {}
  void productRevision(const char*) {}
  void mediaPresent(bool) {}
  void onStartStop(...) {}
  void onRead(...) {}
  void onWrite(...) {}
};
