#pragma once
#include "USBCDC.h"
class ESPUSB {
 public:
  bool begin() { return true; }
  void productName(const char*) {}
  void manufacturerName(const char*) {}
  void serialNumber(const char*) {}
  void onEvent(...) {}
  operator bool() const { return true; }
};
extern ESPUSB USB;
