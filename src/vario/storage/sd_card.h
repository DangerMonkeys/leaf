#pragma once

#include <atomic>
#include "FirmwareMSC.h"
#include "USBMSC.h"
#include "esp_err.h"

enum class SDCardOwnership : uint8_t { FirmwareReserved, FirmwareUploading, HostOwned };

class SDCard {
 public:
  struct FormatResult {
    bool formatted = false;
    bool mounted = false;
    uint8_t mountAttempts = 0;
    const char* stage = "not_started";
    esp_err_t error = ESP_OK;
  };

  void init(bool reserveMassStorage = false);
  void update();

  bool mount();
  void unmount();
  bool format();
  FormatResult formatDetailed();
  FormatResult remountAfterFormat();
  bool setLabel();
  bool isMounted() { return mounted_; }

  bool setupMassStorage(bool mediaPresent = true);
  bool reserveForFirmwareUpload();
  void presentMassStorage();
  void keepMassStorageEjected();
  void notifyExplicitEject();
  bool takeExplicitEject();
  void updateUsbOwnership();
  SDCardOwnership ownership() const { return ownership_.load(std::memory_order_acquire); }
  bool hostOwnsMassStorage() const { return ownership() == SDCardOwnership::HostOwned; }
  static bool isCardPresent();

 private:
  // Whether the SD card is currently mounted (used to compare against the SD_DETECT
  // pin so we can tell if a card has been inserted or removed)
  bool mounted_ = false;

  FirmwareMSC* firmwareMSC_ = nullptr;
  USBMSC* msc_ = nullptr;
  std::atomic<SDCardOwnership> ownership_{SDCardOwnership::FirmwareReserved};
  std::atomic<bool> explicitEject_{false};
  bool reserveMassStorageOnMount_ = false;

  bool formatUnmounted(esp_err_t& error, const char*& stage);
  bool mountWithRetries(uint8_t& attempts);
};

extern SDCard sdcard;
