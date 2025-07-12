#include "hardware/lc86g.h"

void LC86G::init() {
  // TODO: populate
}

bool LC86G::update() {
  return false;  // TODO: populate
}

const char* LC86G::getTextLine() { return currentLine_; }

// Enable GPS Backup Power (to save satellite data and allow faster start-ups)
// This consumes a minor amount of current from the battery
// There is a loop-back pullup resistor from the backup power output to its own ENABLE line, so
// once backup is turned on, it will stay on even if the main processor is shut down. Typically,
// the backup power is only turned off to enable a full cold reboot/reset of the GPS module.
void LC86G::setBackupPower(bool backupPowerOn) {
  // if (backupPowerOn)
  //   digitalWrite(GPS_BACKUP_EN, HIGH);
  // else
  //   digitalWrite(GPS_BACKUP_EN, LOW);
}

void LC86G::sleep() {
  // TODO: populate
}

void LC86G::wake() {
  setBackupPower(true);  // enable backup power if not already
  softReset();
}

void LC86G::hardReset(void) {
  setBackupPower(false);
  softReset();
  setBackupPower(true);
}

void LC86G::softReset(void) {
  // digitalWrite(GPS_RESET, LOW);
  // delay(100);
  // digitalWrite(GPS_RESET, HIGH);
}

void LC86G::enterBackupMode(void) {
  setBackupPower(true);  // ensure backup power enabled (should already be on)
  // TODO: send $PAIR650,0*25 command to shutdown
  // GPS should now draw ~35uA
  // cut main VCC power to further reduce power consumption to ~13uA (i.e., when whole system is
  // shut down)
}

void LC86G::shutdown() {
  sleep();
  setBackupPower(
      0);  // disable GPS backup supply, so when main system shuts down, gps is totally off
}