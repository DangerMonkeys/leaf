#pragma once

#include <Arduino.h>
#include "etl/message_bus.h"

#include "dispatch/message_source.h"
#include "dispatch/message_types.h"
#include "hardware/power_control.h"

// The LC86G class interacts with an LC86G GPS module to present the abstract IGPS hardware
// abstraction interface.
class LC86G : IPowerControl, IMessageSource {
 public:
  // serial: Serial interface via which the LC86G module is controlled and read.
  LC86G(HardwareSerial& serial) : gpsPort_(serial) {}

  void init();
  bool readLine();

  // IPowerControl
  void sleep();
  void wake();

  // IMessageSource
  void attach(etl::imessage_bus* bus) { bus_ = bus; }

 private:
  HardwareSerial& gpsPort_;

  uint32_t bootReady_ = 0;

  size_t newLineIndex_ = 0;
  NMEAString newLine_;

  void setBackupPower(bool backupPowerOn);

  // Fully reset GPS using hardware signals, including powering off backup_power to erase everything
  void hardReset();

  // A soft reset, keeping backup_power enabled so as not to lose saved satellite data
  void softReset();

  void enterBackupMode();

  void shutdown();

  etl::imessage_bus* bus_ = nullptr;
};

extern LC86G lc86g;
