#pragma once

#include "FirmwareMSC.h"
#include "USBMSC.h"
#include "esp_err.h"

class SDCard {
 public:
  struct FormatResult {
    bool formatted = false;
    bool mounted = false;
    uint8_t mountAttempts = 0;
    const char* stage = "not_started";
    esp_err_t error = ESP_OK;
  };

  void init();
  void update();

  bool mount();
  void unmount();
  bool format();
  FormatResult formatDetailed();
  FormatResult remountAfterFormat();
  bool setLabel();
  bool isMounted() { return mounted_; }

  bool setupMassStorage();
  static bool isCardPresent();

 private:
  // Whether the SD card is currently mounted (used to compare against the SD_DETECT
  // pin so we can tell if a card has been inserted or removed)
  bool mounted_ = false;

  FirmwareMSC* firmwareMSC_ = nullptr;
  USBMSC* msc_ = nullptr;

  bool formatUnmounted(esp_err_t& error, const char*& stage);
  bool mountWithRetries(uint8_t& attempts);
};

extern SDCard sdcard;
