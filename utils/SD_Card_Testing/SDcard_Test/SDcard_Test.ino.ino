#include <Arduino.h>
#include <HardwareSerial.h>
#include "FS.h"
#include "SD_MMC.h"

int clk = 36;
int cmd = 35;
int d0 = 37;
int d1 = 38;
int d2 = 33;
int d3 = 34; 

bool fileCreated = false;
uint16_t counter = 0;
uint32_t time_total;
uint32_t time_create;
uint32_t time_append;
uint32_t time_open;
uint32_t time_print;
File openLogFile;


void appendFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Appending to file: %s\n", path);

  time_open = micros();
  File file = fs.open(path, FILE_APPEND);
  time_open = micros() - time_open;
  Serial.print(time_open);
  Serial.println(" - Open");   


  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }

  time_print= micros();
  if (file.print(message)) {
    time_print = micros() - time_print;
    Serial.print(time_print);
    Serial.println(" - Print"); 
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
}

void appendOpenFile(File &f, const char *message) {
  if (f.print(message)) {    
    Serial.println("Message appended!!!");
  } else {
    Serial.println("Append failed!!!");
  }
}


//////////////////////////////////////////////////////////////////////////////////////




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
    Serial.printf("%u bytes read for %lu ms\n", flen, end);
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
  Serial.printf("%u bytes written for %lu ms\n", 2048 * 512, end);
  file.close();
}

void setup() {
  Serial.begin(115200);

  if(!SD_MMC.setPins(clk, cmd, d0, d1, d2, d3)){
      Serial.println("Pin change failed!");
      return;
  }

  if (!SD_MMC.begin()) {
    Serial.println("Card Mount Failed");
    return;
  }
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

  listDir(SD_MMC, "/", 0);
  createDir(SD_MMC, "/mydir");
  listDir(SD_MMC, "/", 0);
  removeDir(SD_MMC, "/mydir");
  listDir(SD_MMC, "/", 2);
  writeFile(SD_MMC, "/hello.txt", "Hello ");
  appendFile(SD_MMC, "/hello.txt", "World!\n");
  readFile(SD_MMC, "/hello.txt");
  deleteFile(SD_MMC, "/foo.txt");
  renameFile(SD_MMC, "/hello.txt", "/foo.txt");
  readFile(SD_MMC, "/foo.txt");
  testFileIO(SD_MMC, "/test.txt");
  Serial.printf("Total space: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));

  delay(3000);
}




void createLogFile() {  
  time_create = micros();
  writeFile(SD_MMC, "/logfile.txt", "<KML Header Info>\n");   
  openLogFile = SD_MMC.open("/logfile.txt", FILE_APPEND);

  time_create = micros() - time_create;
  Serial.print(time_create);
  Serial.println(" - Creating");   

}

void appendDataPoint() {
  time_append = micros();
  //appendFile(SD_MMC, "/logfile.txt", "<New Coordinates and Data>\n");
  appendOpenFile(openLogFile, "<New Coordinates and Data>\n");
  time_append = micros() - time_append;
  Serial.print(time_append);
  Serial.println(" - Appending");   

}

void loop() {
  if (!fileCreated) {
    createLogFile();
    Serial.println("Created File");
    fileCreated = true;
    counter = 0;
  } else {
    if (++counter < 100) {
      time_total = micros();
      appendDataPoint();   
      time_total = micros() - time_total;
      Serial.print(time_total);
      Serial.println(" - Total Time");   
      Serial.print(counter);   
      Serial.println(" - Counter");
    } else {
      while (1) {}
    }
  }
}