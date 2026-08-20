#pragma once

#include <atomic>
#include "FirmwareMSC.h"
#include "USBMSC.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"

enum class SDCardOwnership : uint8_t {
  FirmwareReserved,
  FirmwareUploading,
  TransitioningToHost,
  HostOwned,
  TransitioningToFirmware,
};

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
  bool presentMassStorage();
  bool acquireForFirmwareUse(uint32_t timeoutMs = 2000, bool disconnectUsb = false);
  void keepMassStorageEjected();
  void notifyExplicitEject();
  bool takeExplicitEject();
  void updateUsbOwnership();
  SDCardOwnership ownership() const { return ownership_.load(std::memory_order_acquire); }
  bool hostOwnsMassStorage() const { return ownership() == SDCardOwnership::HostOwned; }
  bool firmwareOwnsMassStorage() const {
    const SDCardOwnership current = ownership();
    return current == SDCardOwnership::FirmwareReserved ||
           current == SDCardOwnership::FirmwareUploading;
  }
  bool firmwareCanAccessFilesystem() const { return mounted_ && firmwareOwnsMassStorage(); }

  // Used only by the USB MSC callbacks to keep a host request alive across an ownership change.
  bool beginHostIo();
  void endHostIo();
  int32_t readHostSectors(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
  int32_t writeHostSectors(uint32_t lba, uint32_t offset, const uint8_t* buffer, uint32_t bufsize);
  static bool isCardPresent();

 private:
  // Whether the SD card is currently mounted (used to compare against the SD_DETECT
  // pin so we can tell if a card has been inserted or removed)
  bool mounted_ = false;

  FirmwareMSC* firmwareMSC_ = nullptr;
  USBMSC* msc_ = nullptr;
  std::atomic<SDCardOwnership> ownership_{SDCardOwnership::FirmwareReserved};
  std::atomic<uint16_t> activeHostIo_{0};
  std::atomic<bool> explicitEject_{false};
  bool reserveMassStorageOnMount_ = false;
  bool mscStarted_ = false;
  bool rawHostInitialized_ = false;
  uint32_t mscSectorCount_ = 0;
  sdmmc_card_t rawHostCard_ = {};
  uint8_t* mscBuffer_ = nullptr;

  bool startRawHost();
  void stopRawHost();
  bool formatUnmounted(esp_err_t& error, const char*& stage);
  bool mountWithRetries(uint8_t& attempts);
};

extern SDCard sdcard;
