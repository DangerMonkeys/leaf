#include <Arduino.h>
#include "FS.h"
#include "SD_MMC.h"

int clk = 36;
int cmd = 35;
int d0 = 37;
int d1 = 38;
int d2 = 33;
int d3 = 34; 

void setup() {
  Serial.begin(115200);
  delay(3000);

  // Setup the SD card
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

  fs::File file = SD_MMC.open("/logfile.txt", FILE_WRITE);

  Serial.println((String)"The size of micros is: " + sizeof(unsigned long));
  while(true) {
    for(int i = 0; i < 1000; i++) {
      auto running_time = micros();
      file.write((const uint8_t*)&running_time, sizeof(unsigned long));
    }
    // Don't flush terribly often
    file.flush();
  }
}

void loop() {
  // Nothing to do
}