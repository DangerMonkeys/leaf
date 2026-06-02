#include "storage/sd_card.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include "FirmwareMSC.h"
#include "USB.h"
#include "USBMSC.h"
#include "hardware/configuration.h"
#include "hardware/io_pins.h"
#include "instruments/gps.h"
#include "logging/log.h"

#define DEBUG_SDCARD true

// Pinout for Leaf V3.2.0
// These should be default pins for ESP32S3, so technically no need to use these and set them.  But
// here for completeness
#define SDIO_D2 33
#define SDIO_D3 34
#define SDIO_CMD 35
#define SDIO_CLK 36
#define SDIO_D0 37
#define SDIO_D1 38

SDCard sdcard;

bool SDCard::isCardPresent() { return !ioexDigitalRead(SD_DETECT_IOEX, SD_DETECT); }

void SDCard::init(void) {
  // Shouldn't need to call set pins since we're using the default pins
  // TODO: try removing this setPins call
  if (!SD_MMC.setPins(SDIO_CLK, SDIO_CMD, SDIO_D0, SDIO_D1, SDIO_D2, SDIO_D3)) {
    Serial.println("Pin change failed!");
    return;
  }

  // configure SD detect pin
  if (!SD_DETECT_IOEX) pinMode(SD_DETECT, INPUT_PULLUP);

  // If SDcard present, mount and save state so we can track changes
  if (isCardPresent()) {
    mounted_ = true;
    sdcard.mount();
  }
}

void SDCard::update() {
  // Don't touch the SD bus from the main loop while a background format is in progress.
  if (format_state_.load() == FormatState::Running) return;

  bool cardPresentNow = isCardPresent();
  // if we have a card when we didn't before...
  if (cardPresentNow && !mounted_) {
    // then mount it!
    if (sdcard.mount()) {
      mounted_ = true;  // save that we have a successfully mounted card
    } else {
      // TODO: Indicate the failure to mount to the user in some way
      Serial.println("WARNING: SD card is present but failed to mount");
    }

    // or if we don't have a card when we DID before, "unmount"
  } else if (!cardPresentNow && mounted_) {
    SD_MMC.end();
    mounted_ = false;  // save that we have a successfully unmounted card
  }
}

namespace {
  int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    // Check bufSize is a multiple of block size
    if (bufsize % 512) {
      return -1;
    }

    auto bufferOffset = 0;
    for (int sector = lba; sector < lba + bufsize / 512; sector++) {
      if (!SD_MMC.readRAW((uint8_t*)buffer + bufferOffset, sector)) {
        return -1;
      }
      bufferOffset += 512;
    }

    return bufsize;
  }

  int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    // Check bufSize is a multiple of block size
    if (bufsize % 512) {
      return -1;
    }

    auto bufferOffset = 0;
    for (int sector = lba; sector < lba + bufsize / 512; sector++) {
      if (!SD_MMC.writeRAW(buffer + bufferOffset, sector)) {
        return -1;
      }
      bufferOffset += 512;
    }

    return bufsize;
  }
}  // namespace

bool SDCard::setupMassStorage() {
  msc_.vendorID("Leaf");
  msc_.productID("Leaf_Vario");
  msc_.productRevision("1.0");
  msc_.onRead(onRead);
  msc_.onWrite(onWrite);
  msc_.isWritable(true);
  msc_.mediaPresent(true);
  msc_.begin(SD_MMC.numSectors(), 512);
  firmwareMSC_.begin();
  return USB.begin();
}

bool SDCard::mount() {
  bool success = false;

  if (!SD_MMC.begin()) {
    if (DEBUG_SDCARD) Serial.println("SDcard Mount Failed");
    success = false;
  } else {
    if (DEBUG_SDCARD) Serial.println("SDcard Mount Success");
    success = true;

#ifndef DISABLE_MASS_STORAGE
    if (sdcard.setupMassStorage()) {
      if (DEBUG_SDCARD) Serial.println("Mass Storage Success");
    } else {
      if (DEBUG_SDCARD) Serial.println("Mass Storage Failed");
    }
#endif
  }

  return success;
}

void SDCard::requestFormat() {
  // Only start a format if one isn't already running. compare_exchange avoids a TOCTOU race where
  // two near-simultaneous requests both spawn a task and both call SD_MMC.end().
  FormatState expected = format_state_.load();
  do {
    if (expected == FormatState::Running) return;  // already formatting
  } while (!format_state_.compare_exchange_weak(expected, FormatState::Running));

  format_message_.store("SD card format in progress...");

  if (xTaskCreate(formatTaskEntry, "SDFormat", 8192, this, 1, &format_task_) != pdPASS) {
    format_task_ = nullptr;
    format_state_.store(FormatState::Failed);
    format_message_.store("Could not start the SD card format task.");
  }
}

void SDCard::formatTaskEntry(void* arg) {
  SDCard* self = static_cast<SDCard*>(arg);
  self->runFormat();
  self->format_task_ = nullptr;
  vTaskDelete(nullptr);
}

void SDCard::runFormat() {
  Serial.println("SDcard: starting format");
  SD_MMC.end();
  mounted_ = false;

  // format_if_mount_failed = true: if the card can't be mounted (corrupt/unformatted), create a
  // partition table and a fresh FAT filesystem, then mount it.
  const bool ok = SD_MMC.begin("/sdcard", false, /*format_if_mount_failed=*/true);
  if (ok) {
    mounted_ = true;
#ifndef DISABLE_MASS_STORAGE
    setupMassStorage();
#endif
    Serial.println("SDcard: format success");
    format_message_.store("SD card formatted successfully.");
    format_state_.store(FormatState::Success);
  } else {
    Serial.println("SDcard: format failed");
    format_message_.store("SD card format failed.");
    format_state_.store(FormatState::Failed);
  }
}
