// The emulated SD card, replacing storage/sd_card.cpp.
//
// The real one drives an SDIO host, an ESP-IDF FAT mount and a USB mass-storage endpoint, none of
// which exist here.  What matters to the rest of the firmware is preserved: a card that can be
// present or absent, mounted or unmounted, with insertion and removal noticed by the periodic
// update() -- backed by a host directory instead of a FAT volume.

#include <Arduino.h>
#include <SD_MMC.h>

#include "hardware/configuration.h"
#include "hardware/io_pins.h"
#include "logging/log.h"
#include "storage/sd_card.h"

SDCard sdcard;

bool SDCard::isCardPresent() { return SD_MMC.simPresent(); }

void SDCard::init(void) {
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

SDCard::FormatResult SDCard::formatDetailed() {
  // Formatting would mean deleting the user's folder of test data.  The emulator declines, and
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
bool SDCard::setupMassStorage() { return false; }

bool SDCard::formatUnmounted(esp_err_t& error, const char*& stage) {
  error = ESP_ERR_NOT_SUPPORTED;
  stage = "not_supported_in_emulator";
  return false;
}

bool SDCard::mountWithRetries(uint8_t& attempts) {
  attempts = 1;
  return mount();
}
