// ESP class stand-in.  Heap numbers are invented but plausible, so the firmware's heap monitor
// and the memory pages have something coherent to display.
#pragma once

#include <stdint.h>

#include "WString.h"

class EspClass {
 public:
  uint32_t getFreeHeap() const;
  uint32_t getMinFreeHeap() const;
  uint32_t getMaxAllocHeap() const;
  uint32_t getHeapSize() const { return 320 * 1024; }
  uint32_t getPsramSize() const { return 8 * 1024 * 1024; }
  uint32_t getFreePsram() const { return 6 * 1024 * 1024; }
  uint32_t getFreeSketchSpace() const { return 3 * 1024 * 1024; }
  uint32_t getSketchSize() const { return 1500 * 1024; }
  uint32_t getCpuFreqMHz() const { return 240; }
  uint32_t getFlashChipSize() const { return 8 * 1024 * 1024; }
  const char* getChipModel() const { return "ESP32-S3 (emulated)"; }
  uint8_t getChipRevision() const { return 0; }
  uint8_t getChipCores() const { return 2; }
  uint64_t getEfuseMac() const { return 0x0102030405ULL; }
  String getSdkVersion() const { return String("leafsim"); }

  // Restarting the emulated device re-runs setup() rather than killing the process.
  void restart();
};

extern EspClass ESP;
