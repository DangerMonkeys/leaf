#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
// #include <PinChangeInterrupt.h>

#include "SDcard.h"
#include "gps.h"
#include "log.h"

#define DEBUG_SDCARD true

// save state of SD card present (used to compare against the SD_DETECT
// pin so we can tell if a card has been inserted or removed)
bool SDcardPresentSavedState = false; 

#include "FirmwareMSC.h"
#include "USB.h"
#include "USBMSC.h"

FirmwareMSC MSC_FirmwareUpdate;
USBMSC MSC;

void listDir(fs::FS& fs, const char* dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void createDir(fs::FS& fs, const char* path) {
  Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path)) {
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}

void removeDir(fs::FS& fs, const char* path) {
  Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path)) {
    Serial.println("Dir removed");
  } else {
    Serial.println("rmdir failed");
  }
}

void readFile(fs::FS& fs, const char* path) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
}

void writeFile(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
}

void appendFile(fs::FS& fs, const char* path, const char* message) {
  uint32_t append_time = micros();

  // Serial.print("startAppend:   ");
  // Serial.println(append_time);

  // Serial.printf("Appending to file: %s\n", path);

  // uint32_t time = micros();

  File file = fs.open(path, FILE_APPEND);

  // time = micros()-time;

  // Serial.print("FileOpen: ");
  // Serial.println(time);

  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
}

void appendOpenFile(File& file, const char* message) {
  if (file.print(message)) {
    // Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
}

void renameFile(fs::FS& fs, const char* path1, const char* path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}

void deleteFile(fs::FS& fs, const char* path) {
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

void testFileIO(fs::FS& fs, const char* path) {
  File file = fs.open(path);
  static uint8_t buf[512];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  if (file) {
    len = file.size();
    size_t flen = len;
    start = millis();
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      len -= toRead;
    }
    end = millis() - start;
    Serial.printf("%u bytes read for %u ms\n", flen, end);
    file.close();
  } else {
    Serial.println("Failed to open file for reading");
  }

  file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  size_t i;
  start = millis();
  for (i = 0; i < 2048; i++) {
    file.write(buf, 512);
  }
  end = millis() - start;
  Serial.printf("%u bytes written for %u ms\n", 2048 * 512, end);
  file.close();
}

/*  Currently removing the SD_DETECT ISR, since we're just polling 
    the pin every second with SDcard_update()
void IRAM_ATTR SDIO_DETECT_ISR() {
  remountSDCard = true;
  Serial.println("SD_DETECT");
}
  */

bool SDcard_checkIfPresent() {
  return !digitalRead(SDIO_DETECT);
}

void SDcard_init(void) {
  // Shouldn't need to call set pins since we're using the default pins
  // TODO: try removing this setPins call
  if (!SD_MMC.setPins(SDIO_CLK, SDIO_CMD, SDIO_D0, SDIO_D1, SDIO_D2, SDIO_D3)) {
    Serial.println("Pin change failed!");
    return;
  }

  // attempt to remount the card if the SDIO_DETECT pin changes  (i.e. card state is changed by
  // inserting or removing)
  pinMode(SDIO_DETECT, INPUT_PULLUP);
  // attachInterrupt(SDIO_DETECT, SDIO_DETECT_ISR, CHANGE);

  // If SDcard present, mount and save state so we can track changes
  if(SDcard_checkIfPresent()) {
    SDcardPresentSavedState = true;
    SDcard_mount();
  } 
}


void SDcard_update() {
  // if we have a card when we didn't before...
  if (SDcard_checkIfPresent() && !SDcardPresentSavedState) {
    // then mount it!
    if (SDcard_mount()) {
      SDcardPresentSavedState = true; // save that we have a successfully mounted card
    }

  // or if we don't have a card when we DID before, "unmount"
  } else if (!SDcard_checkIfPresent() && SDcardPresentSavedState) {
    SD_MMC.end();
    SDcardPresentSavedState = false; // save that we have a successfully unmounted card
  }
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
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

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
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

bool SDCard_SetupMassStorage() {
  // Serial.setDebugOutput(true);
  MSC.vendorID("Leaf");
  MSC.productID("Leaf_Vario");
  MSC.productRevision("1.0");
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.isWritable(true);
  MSC.mediaPresent(true);
  MSC.begin(SD_MMC.numSectors(), 512);
  MSC_FirmwareUpdate.begin();
  return USB.begin();
}

bool SDcard_mount() {
  bool success = false;

  if (!SD_MMC.begin()) {
    if (DEBUG_SDCARD) Serial.println("SDcard Mount Failed");
    success = false;
  } else {
    if (DEBUG_SDCARD) Serial.println("SDcard Mount Success");
    success = true;

    #ifndef DISABLE_MASS_STORAGE
      if(SDCard_SetupMassStorage()) {
        if (DEBUG_SDCARD) Serial.println("Mass Storage Success");
      } else {
        if (DEBUG_SDCARD) Serial.println("Mass Storage Failed");
      }
    #endif
  }

  return success;
}

bool SDcard_present() {
  return SDcardPresentSavedState;
}