#include "hardware/lc86g.h"

// Pinout for Leaf V3.2.0
#define GPS_BACKUP_EN \
  40  // 42 on V3.2.0  // Enable GPS backup power.  Generally always-on, except able to be turned
      // off for a full GPS reset if needed
// pins 43-44 are GPS UART, already enabled by default as "Serial0"
#define GPS_RESET 45
#define GPS_1PPS 46  // INPUT

#define DEBUG_GPS 0

// Setup GPS
#define gpsPort \
  Serial0  // This is the hardware communication port (UART0) for GPS Rx and Tx lines.  We use the
           // default ESP32S3 pins so no need to set them specifically
#define GPSBaud 115200
// #define GPSSerialBufferSize 2048

void LC86G::init() {
  // Set pins
  Serial.print("GPS set pins... ");
  pinMode(GPS_BACKUP_EN, OUTPUT);
  setBackupPower(true);  // by default, enable backup power
  pinMode(GPS_RESET, OUTPUT);
  digitalWrite(GPS_RESET, LOW);
  delay(100);
  digitalWrite(GPS_RESET, HIGH);
  // track when GPS was activated; we can't send any commands sooner
  // than ~285ms (we'll use 300ms)
  bootReady_ = millis() + 300;

  Serial.print("GPS being serial port... ");
  gpsPort.begin(GPSBaud);
  // gpsPort.setRxBufferSize(GPSSerialBufferSize);
}

bool LC86G::update() {
  while (gpsPort.available()) {
    char c = gpsPort.read();
    if (c == '\n' || c == '\r') {
      // End of line
      newLine_[newLineIndex_] = '\0';
      if (newLineIndex_ > 0) {  // Ignore blank lines and second character in CR+LF
        for (size_t i = 0; i <= newLineIndex_; i++) {
          currentLine_[i] = newLine_[i];
        }
        newLineIndex_ = 0;
        return true;  // Return immediately to process the new result even though there may still be
                      // data available from gpsPort
      }
    } else {
      // New character for line
      newLine_[newLineIndex_] = c;
      newLineIndex_++;
      if (newLineIndex_ >= MAX_NMEA_SENTENCE_LENGTH) {
        // This could reasonably happen if there were electrical noise on the serial line
        // and we can recover by simply continuing to wait for a newline after clearing
        // out the current sentence.
        newLine_[MAX_NMEA_SENTENCE_LENGTH] = 0;
        Serial.printf("WARNING: LC86G sentence length exceeded with sentence: '%s'\n", newLine_);
        newLineIndex_ = 0;
      }
    }
  }
  return false;
}

const char* LC86G::getTextLine() { return currentLine_; }

// Enable GPS Backup Power (to save satellite data and allow faster start-ups)
// This consumes a minor amount of current from the battery
// There is a loop-back pullup resistor from the backup power output to its own ENABLE line, so
// once backup is turned on, it will stay on even if the main processor is shut down. Typically,
// the backup power is only turned off to enable a full cold reboot/reset of the GPS module.
void LC86G::setBackupPower(bool backupPowerOn) {
  if (backupPowerOn)
    digitalWrite(GPS_BACKUP_EN, HIGH);
  else
    digitalWrite(GPS_BACKUP_EN, LOW);
}

void LC86G::sleep() {
  uint32_t millisNow = millis();
  uint32_t delayTime = 0;
  if (millisNow < bootReady_) delayTime = bootReady_ - millisNow;
  if (delayTime > 300) delayTime = 300;

  Serial.print("LC86G::sleep ");
  Serial.print("now: ");
  Serial.println(millisNow);
  Serial.print("ready: ");
  Serial.println(bootReady_);
  Serial.print("delay: ");
  Serial.println(delayTime);

  delay(delayTime);  // don't send a command until the GPS is booted up and ready

  gpsPort.write("$PAIR650,0*25\r\n");  // shutdown command
  // delay(100);
  Serial.println("************ !!!!!!!!!!! GPS SLEEPING COMMAND SENT !!!!!!!!!!! ************");
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
  digitalWrite(GPS_RESET, LOW);
  delay(100);
  digitalWrite(GPS_RESET, HIGH);
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
