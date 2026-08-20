// The emulated SD card.
//
// Backed by a host directory, and removable: the emulator can "eject" the card, which makes
// isCardPresent() false and every open fail, so the firmware's card-missing paths are testable.
#pragma once

#include <stdint.h>

#include "FS.h"

class SDMMCFS : public fs::FS {
 public:
  SDMMCFS() : fs::FS("") {}

  bool begin(const char* mountpoint = "/sdcard", bool mode1bit = false, bool formatIfEmpty = false,
             int sdmmcFrequency = 20000, uint8_t maxOpenFiles = 5);
  void end();
  bool setPins(int clk, int cmd, int d0, int d1, int d2, int d3);
  bool setPins(int clk, int cmd, int d0);

  uint64_t cardSize() const;
  uint64_t totalBytes() const;
  uint64_t usedBytes() const;
  uint64_t numSectors() const;
  uint8_t cardType() const;

  bool readRAW(uint8_t* buffer, uint32_t sector);
  bool writeRAW(uint8_t* buffer, uint32_t sector);

  // Emulator-side controls.
  void simSetPresent(bool present);
  bool simPresent() const { return present_; }
  bool mounted() const { return mounted_; }

 private:
  bool present_ = true;
  bool mounted_ = false;
};

#define CARD_NONE 0
#define CARD_MMC 1
#define CARD_SD 2
#define CARD_SDHC 3

extern SDMMCFS SD_MMC;
