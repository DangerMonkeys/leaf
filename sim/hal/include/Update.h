#pragma once
#include <stdint.h>
#include "WString.h"
class UpdateClass {
 public:
  bool begin(size_t size = 0) { return false; }
  size_t write(uint8_t*, size_t) { return 0; }
  bool end(bool evenIfRemaining = false) { return false; }
  bool isFinished() { return false; }
  String errorString() { return String("update not supported in the emulator"); }
};
extern UpdateClass Update;
