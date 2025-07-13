#ifndef SDcard_h
#define SDcard_h

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

// Pinout for Leaf V3.2.0
// These should be default pins for ESP32S3, so technically no need to use these and set them.  But
// here for completeness
#define SDIO_D2 33
#define SDIO_D3 34
#define SDIO_CMD 35
#define SDIO_CLK 36
#define SDIO_D0 37
#define SDIO_D1 38

void listDir(fs::FS& fs, const char* dirname, uint8_t levels);
void createDir(fs::FS& fs, const char* path);
void removeDir(fs::FS& fs, const char* path);
void readFile(fs::FS& fs, const char* path);
void writeFile(fs::FS& fs, const char* path, const char* message);
void appendFile(fs::FS& fs, const char* path, const char* message);
void renameFile(fs::FS& fs, const char* path1, const char* path2);
void deleteFile(fs::FS& fs, const char* path);
void testFileIO(fs::FS& fs, const char* path);

void SDcard_init(void);
void SDcard_update(void);

bool SDcard_mount(void);
bool SDcard_present(void);

bool SDcard_createTrackFile(String filename);
void SDcard_writeLogData(String coordinates);
void appendOpenFile(File& file, const char* message);
void SDcard_writeLogHeader(void);
void SDcard_writeLogFooter(String trackName, String trackDescription);

bool SDcard_createDataFile(String filename);
void SDcard_writeData(String data);
void SDcard_closeDataFile(void);

bool SDCard_SetupMassStorage(void);

#endif
