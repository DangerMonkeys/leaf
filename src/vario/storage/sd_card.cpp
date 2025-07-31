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
