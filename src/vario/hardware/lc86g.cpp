#include "hardware/lc86g.h"

#include "etl/message_bus.h"

#include "dispatch/message_types.h"
#include "hardware/configuration.h"
#include "hardware/io_pins.h"

// Serial0 is the hardware communication port (UART0) for GPS Rx and Tx lines.  We use the
// default ESP32S3 pins so no need to set them specifically
LC86G lc86g(Serial0);

// Pinout for Leaf V3.2.0+
#define GPS_1PPS 46  // INPUT
// pins 43-44 are GPS UART, already enabled by default as "Serial0"

#define DEBUG_GPS 0

// Setup GPS
#define GPSBaud 115200
// #define GPSSerialBufferSize 2048

void LC86G::init() {
  // Ensure we can write individual characters to the full capacity of newLine_
  newLine_.resize(newLine_.capacity());

  // Set pins
  Serial.print("GPS set pins... ");
  if (!GPS_BACKUP_EN_IOEX) pinMode(GPS_BACKUP_EN, OUTPUT);
  setBackupPower(true);  // by default, enable backup power
  if (!GPS_RESET_IOEX) pinMode(GPS_RESET, OUTPUT);
  ioexDigitalWrite(GPS_RESET_IOEX, GPS_RESET, LOW);
  delay(100);
  ioexDigitalWrite(GPS_RESET_IOEX, GPS_RESET, HIGH);
  // track when GPS was activated; we can't send any commands sooner
  // than ~285ms (we'll use 300ms)
  bootReady_ = millis() + 300;

  Serial.print("GPS being serial port... ");
  gpsPort_.begin(GPSBaud);
  // gpsPort.setRxBufferSize(GPSSerialBufferSize);

  // Serial.println("Setting GPS messages");

  /*
    gps.send_P( &gpsPort, (const __FlashStringHelper *) enableGGA );
    delay(50);
    gps.send_P( &gpsPort, (const __FlashStringHelper *) enableGSV );
    delay(50);
    gps.send_P( &gpsPort, (const __FlashStringHelper *) enableRMC );
    delay(50);
    gps.send_P( &gpsPort, (const __FlashStringHelper *) disableGLL );
    delay(50);
    gps.send_P( &gpsPort, (const __FlashStringHelper *) disableGSA );
    delay(50);
    gps.send_P( &gpsPort, (const __FlashStringHelper *) disableVTG );
    delay(50);
  */

  /*
    gpsPort.println("$PAIR062,0,1*3F");	//turn on GGA at 1 sec
    gpsPort.println("$PAIR062,1,0*3F");	//turn off GLL
    gpsPort.println("$PAIR062,2,0*3C");	//turn off GSA
    gpsPort.println("$PAIR062,3,4*39");	//turn on GSV at 4 sec (up to 3 sentences)  //was 00,01*23"
    gpsPort.println("$PAIR062,4,1*3B");	//turn on RMC at 1 sec
    gpsPort.println("$PAIR062,5,0*3B");	//turn off VTG
  */

  /*
  0 = NMEA_SEN_GGA    $PAIR062,0,0*3E   $PAIR062,0,1*3F
  1 = NMEA_SEN_GLL    $PAIR062,1,0*3F   $PAIR062,1,1*3E
  2 = NMEA_SEN_GSA    $PAIR062,2,0*3C   $PAIR062,2,1*3D
  3 = NMEA_SEN_GSV    $PAIR062,3,0*3D   $PAIR062,3,1*3C   $PAIR062,3,4*39
  4 = NMEA_SEN_RMC    $PAIR062,4,0*3A   $PAIR062,4,1*3B
  5 = NMEA_SEN_VTG    $PAIR062,5,0*3B   $PAIR062,5,1*3A
  6 = NMEA_SEN_ZDA    $PAIR062,6,0*38   $PAIR062,6,1*39
  7 = NMEA_SEN_GRS    $PAIR062,7,0*39   $PAIR062,7,1*38
  8 = NMEA_SEN_GST    $PAIR062,8,0*36   $PAIR062,8,1*37
  9 = NMEA_SEN_GNS    $PAIR062,9,0*37   $PAIR062,9,1*36

  */
}

bool LC86G::readLine() {
  while (gpsPort_.available()) {
    char c = gpsPort_.read();
    if (c == '\n' || c == '\r') {
      // End of line
      newLine_[newLineIndex_] = '\0';
      if (newLineIndex_ > 0) {  // Ignore blank lines and second character in CR+LF
        newLine_.resize(newLineIndex_);
        if (bus_) {
          bus_->receive(GpsMessage(newLine_));  // Send the complete NMEA sentence to the bus
        }
        newLine_.resize(newLine_.capacity());
        newLineIndex_ = 0;
        return true;  // Return immediately after the line even though there may still be
                      // data available from gpsPort
      }
    } else {
      // New character for line
      newLine_[newLineIndex_] = c;
      newLineIndex_++;
      if (newLineIndex_ >= newLine_.capacity()) {
        // This could reasonably happen if there were electrical noise on the serial line
        // and we can recover by simply continuing to wait for a newline after clearing
        // out the current sentence.
        Serial.printf(
            "WARNING: LC86G sentence length %d was exceeded with sentence (index %d length %d): "
            "'%s'\n",
            newLine_.capacity(), newLineIndex_, newLine_.length(), newLine_.c_str());
        newLineIndex_ = 0;
      }
    }
  }
  return false;
}

// Enable GPS Backup Power (to save satellite data and allow faster start-ups)
// This consumes a minor amount of current from the battery
// There is a loop-back pullup resistor from the backup power output to its own ENABLE line, so once
// backup is turned on, it will stay on even if the main processor is shut down. Typically, the
// backup power is only turned off to enable a full cold reboot/reset of the GPS module.
void LC86G::setBackupPower(bool backupPowerOn) {
  if (backupPowerOn)
    ioexDigitalWrite(GPS_BACKUP_EN_IOEX, GPS_BACKUP_EN, HIGH);
  else
    ioexDigitalWrite(GPS_BACKUP_EN_IOEX, GPS_BACKUP_EN, LOW);
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

  gpsPort_.write("$PAIR650,0*25\r\n");  // shutdown command
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

// A soft reset, keeping backup_power enabled so as not to lose saved satellite data
void LC86G::softReset(void) {
  ioexDigitalWrite(GPS_RESET_IOEX, GPS_RESET, LOW);
  delay(100);
  ioexDigitalWrite(GPS_RESET_IOEX, GPS_RESET, HIGH);
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
