// Capture-only SD stand-in, derived from sim/device/sd_card.cpp.
// Firmware filesystem behavior is retained; catalog flags control USB handoff and
// delayed ownership return. This object is linked only into the catalog executable.

#include <Arduino.h>
#include <SD_MMC.h>

#include "hardware/configuration.h"
#include "hardware/io_pins.h"
#include "logging/log.h"
#include "storage/sd_card.h"

namespace catalog {
  extern bool usbAvailable;
  extern bool acquireBlocked;
}  // namespace catalog
SDCard sdcard;

bool SDCard::isCardPresent() { return SD_MMC.simPresent(); }

void SDCard::init(bool reserveMassStorage) {
  reserveMassStorageOnMount_ = reserveMassStorage;
  Serial.println("SD card: emulated (backed by a host directory)");
  mount();
}

bool SDCard::mount() {
  if (!isCardPresent()) {
    mounted_ = false;
    Serial.println("SD card: no card inserted");
    return false;
  }
  mounted_ = SD_MMC.begin();
  Serial.printf("SD card: %s\n", mounted_ ? "mounted" : "mount failed");
  return mounted_;
}

void SDCard::unmount() {
  SD_MMC.end();
  mounted_ = false;
}

void SDCard::update() {
  // Same contract as the device: notice a card that appeared or disappeared since last time.
  const bool present = isCardPresent();
  if (present && !mounted_) {
    mount();
  } else if (!present && mounted_) {
    Serial.println("SD card: removed");
    unmount();
  }
}

bool SDCard::format() { return formatDetailed().formatted; }

SDCard::FormatResult SDCard::formatDetailed(bool /*remount*/) {
  // Formatting would mean deleting the user's folder of test data. The emulator declines, and
  // says so, rather than quietly reporting success it did not achieve.
  FormatResult result;
  result.stage = "not_supported_in_emulator";
  result.error = ESP_ERR_NOT_SUPPORTED;
  result.mounted = mounted_;
  Serial.println("SD card: format is not supported in the emulator");
  return result;
}

SDCard::FormatResult SDCard::remountAfterFormat() { return formatDetailed(); }

bool SDCard::setLabel() { return true; }

// The catalog supplies controlled handoff outcomes without a real USB host.
bool SDCard::setupMassStorage(bool mediaPresent) {
  (void)mediaPresent;
  return false;
}

bool SDCard::presentMassStorage() {
  if (!catalog::usbAvailable) return false;
  ownership_.store(SDCardOwnership::HostOwned);
  return true;
}

bool SDCard::reserveForFirmwareUpload() {
  SDCardOwnership expected = SDCardOwnership::FirmwareReserved;
  return ownership_.compare_exchange_strong(expected, SDCardOwnership::FirmwareUploading,
                                            std::memory_order_acq_rel);
}

bool SDCard::acquireForFirmwareUse(uint32_t timeoutMs, bool disconnectUsb) {
  if (catalog::acquireBlocked) return false;
  (void)timeoutMs;
  (void)disconnectUsb;
  ownership_.store(SDCardOwnership::FirmwareReserved, std::memory_order_release);
  return true;
}

void SDCard::keepMassStorageEjected() {}

void SDCard::notifyExplicitEject() {}

bool SDCard::takeExplicitEject() { return false; }

void SDCard::updateUsbOwnership() {}

bool SDCard::beginHostIo() { return false; }

void SDCard::endHostIo() {}

int32_t SDCard::readHostSectors(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  (void)lba;
  (void)offset;
  (void)buffer;
  (void)bufsize;
  return -1;
}

int32_t SDCard::writeHostSectors(uint32_t lba, uint32_t offset, const uint8_t* buffer,
                                 uint32_t bufsize) {
  (void)lba;
  (void)offset;
  (void)buffer;
  (void)bufsize;
  return -1;
}

bool SDCard::formatUnmounted(esp_err_t& error, const char*& stage) {
  error = ESP_ERR_NOT_SUPPORTED;
  stage = "not_supported_in_emulator";
  return false;
}

bool SDCard::mountWithRetries(uint8_t& attempts) {
  attempts = 1;
  return mount();
}
