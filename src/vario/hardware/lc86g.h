#pragma once

#include "hardware/gps.h"

#include <Arduino.h>

// Maximum length of an NMEA sentence that can be read by this class
const size_t MAX_NMEA_SENTENCE_LENGTH = 82 + 2;

// The LC86G class interacts with an LC86G GPS module to present the abstract IGPS hardware
// abstraction interface.
class LC86G : public IGPS {
 public:
  // serial: Serial interface via which the LC86G module is controlled and read.
  LC86G(HardwareSerial* serial) : serial_(serial) { currentLine_[0] = 0; }

  // IGPS:ITextLineSource
  void init();
  bool update();
  const char* getTextLine();

  // IGPS:IPowerControl
  void sleep();
  void wake();

 private:
  HardwareSerial* serial_;

  uint32_t bootReady_ = 0;

  size_t newLineIndex_ = 0;
  char newLine_[MAX_NMEA_SENTENCE_LENGTH + 1];
  char currentLine_[MAX_NMEA_SENTENCE_LENGTH + 1];

  void setBackupPower(bool backupPowerOn);

  // Fully reset GPS using hardware signals, including powering off backup_power to erase everything
  void hardReset();

  // A soft reset, keeping backup_power enabled so as not to lose saved satellite data
  void softReset();

  void enterBackupMode();

  void shutdown();
};
