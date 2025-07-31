#pragma once

#include "FirmwareMSC.h"
#include "USBMSC.h"

class SDCard {
 public:
  void init();
  void update();

  bool mount();
  bool isMounted() { return mounted_; }

  bool setupMassStorage();

 private:
  static bool isCardPresent();

  // Whether the SD card is currently mounted (used to compare against the SD_DETECT
  // pin so we can tell if a card has been inserted or removed)
  bool mounted_ = false;

  FirmwareMSC firmwareMSC_;
  USBMSC msc_;
};

extern SDCard sdcard;
