#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
// #include <PinChangeInterrupt.h>

#include "SDcard.h"
#include "gps.h"
#include "log.h"

#define DEBUG_SDCARD true

bool remountSDCard = false;  // flag set if the SD_DETECT pin changes state, so we know to attempt a
                             // re-mounting if the card is inserted or removed
bool SDcardIsPresent = false;

#include "USB.h"
#include "USBMSC.h"
#include "FirmwareMSC.h"

FirmwareMSC MSC_FirmwareUpdate;
USBMSC MSC;

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
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

void createDir(fs::FS &fs, const char *path) {
  Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path)) {
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}

void removeDir(fs::FS &fs, const char *path) {
  Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path)) {
    Serial.println("Dir removed");
  } else {
    Serial.println("rmdir failed");
  }
}

void readFile(fs::FS &fs, const char *path) {
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

void writeFile(fs::FS &fs, const char *path, const char *message) {
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

void appendFile(fs::FS &fs, const char *path, const char *message) {
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

void appendOpenFile(File &file, const char *message) {
  if (file.print(message)) {
    // Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
}

void renameFile(fs::FS &fs, const char *path1, const char *path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}

void deleteFile(fs::FS &fs, const char *path) {
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

void testFileIO(fs::FS &fs, const char *path) {
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

void IRAM_ATTR SDIO_DETECT_ISR() {
  remountSDCard = true;
  Serial.println("SD_DETECT");
}

void SDcard_init(void) {
  // Shouldn't need to call set pins since we're using the default pins TODO: try removing this
  // setPins call
  if (!SD_MMC.setPins(SDIO_CLK, SDIO_CMD, SDIO_D0, SDIO_D1, SDIO_D2, SDIO_D3)) {
    Serial.println("Pin change failed!");
    return;
  }

  // attempt to remount the card if the SDIO_DETECT pin changes  (i.e. card state is changed by
  // inserting or removing)
  pinMode(SDIO_DETECT, INPUT_PULLUP);
  attachInterrupt(SDIO_DETECT, SDIO_DETECT_ISR, CHANGE);

  SDcard_mount();
}

// TODO: we probably don't need ISR anymore to manage SD card since we're just checking the state
// change each second
uint8_t SDcard_detectState = 1;  // flag for SD card insertion state (assume SD card is inserted)
uint8_t SDcard_detectStateLast = 1;  // flag so we can see if there was a state change (and remount
                                     // card if inserted while in charging state)
// called every second in case the SD card state has changed and we need to try remounting
void SDcard_update() {
  SDcard_detectState = !digitalRead(SDIO_DETECT);

  if (remountSDCard || (SDcard_detectState && !SDcard_detectStateLast)) {
    SDcard_mount();
    remountSDCard = false;
  }

  SDcard_detectStateLast = SDcard_detectState;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    // Check bufSize is a multiple of block size
    if (bufsize % 512) {
        return -1;
    }

    auto bufferOffset = 0;
    for(int sector = lba; sector < lba + bufsize / 512; sector++) {
        if (!SD_MMC.readRAW((uint8_t*)buffer + bufferOffset, sector)) {
            return -1;
        }
        bufferOffset += 512;
    }
    
    return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    // Check bufSize is a multiple of block size
    if (bufsize % 512) {
        return -1;
    }

    auto bufferOffset = 0;
    for(int sector = lba; sector < lba + bufsize / 512; sector++) {
        if (!SD_MMC.writeRAW(buffer + bufferOffset, sector)) {
            return -1;
        }
        bufferOffset += 512;
    }
    
    return bufsize;
}

void SDCard_SetupMassStorage() {
    Serial.setDebugOutput(true);
    MSC.vendorID("Leaf");
    MSC.productID("Leaf_Vario");
    MSC.productRevision("1.0");
    MSC.onRead(onRead);
    MSC.onWrite(onWrite);
    MSC.isWritable(true);
    MSC.mediaPresent(true);
    MSC.begin(SD_MMC.numSectors(), 512);
    MSC_FirmwareUpdate.begin();
    USB.begin();
}

bool SDcard_mount() {
  // we may be re-mounting after changing power states (from ON to CHARGE and back to ON).
  // If the SD card was removed during that process, we need to force-end the SD_MCC object so we
  // can try to restart it, so we can properly tell if re-mounting fails (otherwise the SD_MCC
  // object would falsely persist)
  SD_MMC.end();

  if (!SD_MMC.begin()) {
    if (DEBUG_SDCARD) Serial.println("SDcard Mount Failed");
    SDcardIsPresent = false;
  } else {
    if (DEBUG_SDCARD) Serial.println("SDcard Mount Success");
    SDcardIsPresent = true;
    SDCard_SetupMassStorage();
  }
  
  return SDcardIsPresent;
}

bool SDcard_present() { return SDcardIsPresent; }

void SDcard_test(void) {
  Serial.println("SD test stuff");
  SDcard_testStuff();

  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD_MMC card attached");
    return;
  }

  Serial.print("SD_MMC Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);

  // listDir(SD_MMC, "/", 2);
  createDir(SD_MMC, "/mydir");
  listDir(SD_MMC, "/", 2);
  // removeDir(SD_MMC, "/mydir");
  listDir(SD_MMC, "/", 2);
  writeFile(SD_MMC, "/mydir/hello.txt", "Hello ");
  listDir(SD_MMC, "/", 2);
  createDir(SD_MMC, "/mydir");
  listDir(SD_MMC, "/", 2);

  // appendFile(SD_MMC, "/hello.txt", "World!\n");
  // readFile(SD_MMC, "/hello.txt");
  // deleteFile(SD_MMC, "/foo.txt");
  // renameFile(SD_MMC, "/hello.txt", "/foo.txt");
  // readFile(SD_MMC, "/foo.txt");
  testFileIO(SD_MMC, "/test.txt");
  Serial.printf("Total space: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));
}

void SDcard_testStuff() {
  Serial.print("isValid: ");
  Serial.print(gps.time.isValid());
  Serial.print(", isUpdated:");
  Serial.print(gps.time.isUpdated());
  Serial.print(", hour: ");
  Serial.print(gps.time.hour());
  Serial.print(", minute: ");
  Serial.print(gps.time.minute());
  Serial.print(", second: ");
  Serial.print(gps.time.second());
  Serial.print(", age: ");
  Serial.print(gps.time.age());
  Serial.print(", value: ");
  Serial.println(gps.time.value());
}
