#include "storage/sd_card.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <ff.h>
#include <sdmmc_cmd.h>
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

constexpr DWORD SD_CARD_FORMAT_ALLOCATION_UNIT_SIZE = 32768;
constexpr auto SD_CARD_VOLUME_LABEL = "LEAF VARIO";
constexpr auto SD_CARD_MOUNT_POINT = "/sdcard";
constexpr auto SD_CARD_FORMAT_MOUNT_POINT = "/sdcard_format";

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
    mounted_ = sdcard.mount();
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
    unmount();
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
  const uint64_t sectorCount = SD_MMC.cardSize() / 512;
  if (sectorCount == 0) {
    if (DEBUG_SDCARD) Serial.println("Mass Storage Failed: SD card size is unknown");
    return false;
  }

  msc_.vendorID("Leaf");
  msc_.productID("Leaf_Vario");
  msc_.productRevision("1.0");
  msc_.onRead(onRead);
  msc_.onWrite(onWrite);
  msc_.isWritable(true);
  msc_.mediaPresent(true);
  msc_.begin(sectorCount, 512);
  firmwareMSC_.begin();
  return USB.begin();
}

bool SDCard::mount() {
  bool success = false;

  if (!SD_MMC.begin(SD_CARD_MOUNT_POINT, false, false)) {
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

void SDCard::unmount() {
  SD_MMC.end();
  mounted_ = false;
}

bool SDCard::format() {
  if (!isCardPresent()) {
    if (DEBUG_SDCARD) Serial.println("SDcard Format Failed: no card present");
    return false;
  }

  if (mounted_) {
    unmount();
  }

  if (DEBUG_SDCARD) Serial.println("Formatting SDcard");

  if (!formatUnmounted()) {
    return false;
  }

  mounted_ = mount();
  return mounted_;
}

bool SDCard::formatUnmounted() {
  if (DEBUG_SDCARD) Serial.println("Formatting unmounted SDcard");

  SD_MMC.end();
  SD_MMC.setPins(SDIO_CLK, SDIO_CMD, SDIO_D0, SDIO_D1, SDIO_D2, SDIO_D3);

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_4BIT;
  host.slot = SDMMC_HOST_SLOT_1;

  sdmmc_slot_config_t slotConfig = SDMMC_SLOT_CONFIG_DEFAULT();
  slotConfig.clk = static_cast<gpio_num_t>(SDIO_CLK);
  slotConfig.cmd = static_cast<gpio_num_t>(SDIO_CMD);
  slotConfig.d0 = static_cast<gpio_num_t>(SDIO_D0);
  slotConfig.d1 = static_cast<gpio_num_t>(SDIO_D1);
  slotConfig.d2 = static_cast<gpio_num_t>(SDIO_D2);
  slotConfig.d3 = static_cast<gpio_num_t>(SDIO_D3);
  slotConfig.width = 4;

  esp_vfs_fat_sdmmc_mount_config_t mountConfig = {};
  mountConfig.format_if_mount_failed = false;
  mountConfig.max_files = 1;
  mountConfig.allocation_unit_size = SD_CARD_FORMAT_ALLOCATION_UNIT_SIZE;
  mountConfig.disk_status_check_enable = false;
  mountConfig.use_one_fat = false;

  sdmmc_card_t* card = nullptr;
  esp_err_t result = esp_vfs_fat_sdmmc_mount(SD_CARD_FORMAT_MOUNT_POINT, &host, &slotConfig, &mountConfig, &card);
  if (result != ESP_OK) {
    SD_MMC.end();
    SD_MMC.setPins(SDIO_CLK, SDIO_CMD, SDIO_D0, SDIO_D1, SDIO_D2, SDIO_D3);
    mountConfig.format_if_mount_failed = true;
    result = esp_vfs_fat_sdmmc_mount(SD_CARD_FORMAT_MOUNT_POINT, &host, &slotConfig, &mountConfig, &card);
    if (result != ESP_OK) {
      if (DEBUG_SDCARD) Serial.printf("SDcard Format Failed: mount/format returned 0x%x\n", result);
      return false;
    }
  } else {
    result = esp_vfs_fat_sdcard_format_cfg(SD_CARD_FORMAT_MOUNT_POINT, card, &mountConfig);
    if (result != ESP_OK) {
      if (DEBUG_SDCARD) Serial.printf("SDcard Format Failed: format returned 0x%x\n", result);
      esp_vfs_fat_sdcard_unmount(SD_CARD_FORMAT_MOUNT_POINT, card);
      return false;
    }
  }

  result = esp_vfs_fat_sdcard_unmount(SD_CARD_FORMAT_MOUNT_POINT, card);
  if (result != ESP_OK) {
    if (result != ESP_ERR_INVALID_STATE) {
      if (DEBUG_SDCARD) Serial.printf("SDcard Format Failed: unmount returned 0x%x\n", result);
      return false;
    }

    if (DEBUG_SDCARD) Serial.println("SDcard Format Cleanup: temporary mount already released");
  }

  SD_MMC.end();
  SD_MMC.setPins(SDIO_CLK, SDIO_CMD, SDIO_D0, SDIO_D1, SDIO_D2, SDIO_D3);
  return true;
}

bool SDCard::setLabel() {
  if (!mounted_) {
    if (DEBUG_SDCARD) Serial.println("SDcard Label Failed: card is not mounted");
    return false;
  }

  FRESULT result = f_setlabel(SD_CARD_VOLUME_LABEL);
  if (result != FR_OK) {
    if (DEBUG_SDCARD) Serial.printf("SDcard Label Failed: f_setlabel returned %d\n", result);
    return false;
  }

  if (DEBUG_SDCARD) Serial.println("SDcard Label Success");
  return true;
}
