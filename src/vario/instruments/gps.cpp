/*
 * gps.cpp
 *
 *
 */
#include "instruments/gps.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "comms/message_types.h"
#include "hardware/configuration.h"
#include "hardware/io_pins.h"
#include "instruments/baro.h"
#include "logging/log.h"
#include "logging/telemetry.h"
#include "navigation/gpx.h"
#include "storage/sd_card.h"
#include "ui/display/display.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

LeafGPS gps;

// Pinout for Leaf V3.2.0+
#define GPS_1PPS 46  // INPUT
// pins 43-44 are GPS UART, already enabled by default as "Serial0"

#define DEBUG_GPS 0

// Setup GPS
#define gpsPort \
  Serial0  // This is the hardware communication port (UART0) for GPS Rx and Tx lines.  We use the
           // default ESP32S3 pins so no need to set them specifically
#define GPSBaud 115200
// #define GPSSerialBufferSize 2048

const char enableGGA[] PROGMEM = "$PAIR062,0,1";  // enable GGA message every 1 second
const char enableGSV[] PROGMEM = "PAIR062,3,4";   // enable GSV message every 1 second
const char enableRMC[] PROGMEM = "PAIR062,4,1";   // enable RMC message every 1 second

const char disableGLL[] PROGMEM = "$PAIR062,1,0";  // disable message
const char disableGSA[] PROGMEM = "$PAIR062,2,0";  // disable message
const char disableVTG[] PROGMEM = "$PAIR062,5,0";  // disable message

// Lock for GPS
SemaphoreHandle_t GpsLockGuard::mutex = NULL;

// Enable GPS Backup Power (to save satellite data and allow faster start-ups)
// This consumes a minor amount of current from the battery
// There is a loop-back pullup resistor from the backup power output to its own ENABLE line, so once
// backup is turned on, it will stay on even if the main processor is shut down. Typically, the
// backup power is only turned off to enable a full cold reboot/reset of the GPS module.
void LeafGPS::setBackupPower(bool backupPowerOn) {
  if (backupPowerOn)
    ioexDigitalWrite(GPS_BACKUP_EN_IOEX, GPS_BACKUP_EN, HIGH);
  else
    ioexDigitalWrite(GPS_BACKUP_EN_IOEX, GPS_BACKUP_EN, LOW);
}

// Fully reset GPS using hardware signals, including powering off backup_power to erase everything
void LeafGPS::hardReset(void) {
  setBackupPower(false);
  softReset();
  setBackupPower(true);
}

// A soft reset, keeping backup_power enabled so as not to lose saved satellite data
void LeafGPS::softReset(void) {
  ioexDigitalWrite(GPS_RESET_IOEX, GPS_RESET, LOW);
  delay(100);
  ioexDigitalWrite(GPS_RESET_IOEX, GPS_RESET, HIGH);
}

void LeafGPS::enterBackupMode(void) {
  setBackupPower(true);  // ensure backup power enabled (should already be on)
  // TODO: send $PAIR650,0*25 command to shutdown
  // GPS should now draw ~35uA
  // cut main VCC power to further reduce power consumption to ~13uA (i.e., when whole system is
  // shut down)
}

void LeafGPS::sleep() {
  uint32_t millisNow = millis();
  uint32_t delayTime = 0;
  if (millisNow < bootReady) delayTime = bootReady - millisNow;
  if (delayTime > 300) delayTime = 300;

  Serial.print("now: ");
  Serial.println(millisNow);
  Serial.print("ready: ");
  Serial.println(bootReady);
  Serial.print("delay: ");
  Serial.println(delayTime);

  delay(delayTime);  // don't send a command until the GPS is booted up and ready

  gpsPort.write("$PAIR650,0*25\r\n");  // shutdown command
  // delay(100);
  Serial.println("************ !!!!!!!!!!! GPS SLEEPING COMMAND SENT !!!!!!!!!!! ************");
}

void LeafGPS::wake() {
  setBackupPower(1);  // enable backup power if not already
  softReset();
}

void LeafGPS::shutdown() {
  sleep();
  setBackupPower(
      0);  // disable GPS backup supply, so when main system shuts down, gps is totally off
}

LeafGPS::LeafGPS() {
  totalGPGSVMessages.begin(gps, "GPGSV", 1);
  messageNumber.begin(gps, "GPGSV", 2);
  satsInView.begin(gps, "GPGSV", 3);

  latAccuracy.begin(gps, "GPGST", 6);
  lonAccuracy.begin(gps, "GPGST", 7);
  fix.begin(gps, "GNGGA", 6);
  fixMode.begin(gps, "GNGSA", 2);
}

void LeafGPS::init(void) {
  // Create the GPS Mutex for multi-threaded locking
  GpsLockGuard::mutex = xSemaphoreCreateMutex();

  // init nav class (TODO: may not need this here, just for testing at startup for ease)
  navigator.init();

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
  bootReady = millis() + 300;

  Serial.print("GPS being serial port... ");
  gpsPort.begin(GPSBaud);
  // gpsPort.setRxBufferSize(GPSSerialBufferSize);

  // Initialize all the uninitialized TinyGPSCustom objects
  Serial.print("GPS initialize sat messages... ");
  for (int i = 0; i < 4; ++i) {
    satNumber[i].begin(gps, "GPGSV", 4 + 4 * i);  // offsets 4, 8, 12, 16
    elevation[i].begin(gps, "GPGSV", 5 + 4 * i);  // offsets 5, 9, 13, 17
    azimuth[i].begin(gps, "GPGSV", 6 + 4 * i);    // offsets 6, 10, 14, 18
    snr[i].begin(gps, "GPGSV", 7 + 4 * i);        // offsets 7, 11, 15, 19
  }

  Serial.print("GPS initialize done... ");

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
}  // gps_init

void LeafGPS::calculateGlideRatio() {
  float climb = baro.climbRateAverage;
  float speed = gps.speed.kmph();

  if (baro.climbRateAverage == 0 || speed == 0) {
    glideRatio = 0;
  } else {
    //             km per hour       / cm per sec * sec per hour / cm per km
    glideRatio = navigator.averageSpeed /
                 (-1 * climb * 3600 /
                  100000);  // add -1 to invert climbrate because 'negative' is down (in climb), but
                            // we want a standard glide ratio (ie 'gliding down') to be positive
  }
}

void LeafGPS::updateFixInfo() {
  // fix status and mode
  fixInfo.fix = atoi(fix.value());
  fixInfo.fixMode = atoi(fixMode.value());

  // solution accuracy
  // gpsFixInfo.latError = 2.5; //atof(latAccuracy.value());
  // gpsFixInfo.lonError = 1.5; //atof(lonAccuracy.value());
  // gpsFixInfo.error = sqrt(gpsFixInfo.latError * gpsFixInfo.latError +
  //                         gpsFixInfo.lonError * gpsFixInfo.lonError);
  fixInfo.error = (float)hdop.value() * 5 / 100;
}

void LeafGPS::update() {
  // update sats if we're tracking sat NMEA sentences
  navigator.update();
  updateSatList();
  updateFixInfo();
  calculateGlideRatio();

  String gpsName = "gps,";
  String gpsEntryString = gpsName + String(gps.location.lat(), 8) + ',' +
                          String(gps.location.lng(), 8) + ',' + String(gps.altitude.meters()) +
                          ',' + String(gps.speed.mps()) + ',' + String(gps.course.deg());

  Telemetry.writeText(gpsEntryString);

  /*

  Serial.print("Valid: ");
  Serial.print(gps.course.isValid());
  Serial.print(" Course: ");
  Serial.print(gps.course.deg());
  Serial.print(", Speed: ");
  Serial.print(gps.speed.mph());
  Serial.print(",    AltValid: ");
  Serial.print(gps.altitude.isValid());
  Serial.print(", GPS_alt: ");
  Serial.println(gps.altitude.meters());
  */
}

// this is called whenever a new valid NMEA sentence contains a valid speed (TODO: check if true for
// every sentence or just every fix)
void onNewSentence(NMEASentenceContents contents) { windEstimate_onNewSentence(contents); }

bool LeafGPS::readBufferOnce() {
  GpsLockGuard mutex;  // Ensure we have a lock on write
  if (gpsPort.available()) {
    char a = gpsPort.read();

    // Construct the NMEA sentence in the buffer if this is a NMEA string
    if (nmeaBuffer[0] == '\0' && a == '$') {
      // Serial.println("Starting new NMEA sentence");
      nmeaBufferIndex = 1;
      nmeaBuffer[0] = '$';  // start a new NMEA sentence
    } else if (nmeaBuffer[0] == '$') {
      // We're currently building a NMEA sentence, so add the character to the buffer
      nmeaBuffer[nmeaBufferIndex++] = a;
      if (a == '\r') {
        // If we reach the end of a sentence, null-terminate it
        nmeaBuffer[nmeaBufferIndex++] = '\n';
        nmeaBuffer[nmeaBufferIndex] = '\0';

        bus_->receive(GpsMessage(nmeaBuffer));  // Send the complete NMEA sentence to the bus
        nmeaBufferIndex = 0;                    // reset the index for next sentence
        nmeaBuffer[0] = '\0';                   // null-terminate the string
      }
      if (nmeaBufferIndex >= sizeof(nmeaBuffer) - 1) {
        // Buffer overflow, reset the buffer
        nmeaBufferIndex = 0;
        nmeaBuffer[0] = '\0';  // reset the buffer
        if (DEBUG_GPS) Serial.println("NMEA buffer overflow, resetting.");
      }
    }

    // Serial.print(a);
    // nmeaBuffer[nmeaBufferIndex++] = a;
    bool newSentence = gps.encode(a);
    if (newSentence) {
      NMEASentenceContents contents = {.speed = gps.speed.isUpdated(),
                                       .course = gps.course.isUpdated()};
      // Push the update onto the bus!
      if (bus_ && gps.location.isUpdated()) {
        bus_->receive(GpsReading(gps));
      }

      nmeaBufferIndex = 0;                 // reset the index for next sentence
      nmeaBuffer[nmeaBufferIndex] = '\0';  // null-terminate the string

      onNewSentence(contents);
    }

    if (DEBUG_GPS) Serial.print(a);
    return true;
  } else {
    return false;
  }
}

// copy data from each satellite message into the sats[] array.  Then, if we reach the complete set
// of sentences, copy the fresh sat data into the satDisplay[] array for showing on LCD screen when
// needed.
void LeafGPS::updateSatList() {
  // copy data if we have a complete single sentence
  if (totalGPGSVMessages.isUpdated()) {
    for (int i = 0; i < 4; ++i) {
      int no = atoi(satNumber[i].value());
      // Serial.print(F("SatNumber is ")); Serial.println(no);
      if (no >= 1 && no <= MAX_SATELLITES) {
        sats[no - 1].elevation = atoi(elevation[i].value());
        sats[no - 1].azimuth = atoi(azimuth[i].value());
        sats[no - 1].snr = atoi(snr[i].value());
        sats[no - 1].active = true;
      }
    }

    // If we're on the final sentence, then copy data into the display array
    int totalMessages = atoi(totalGPGSVMessages.value());
    int currentMessage = atoi(messageNumber.value());
    if (totalMessages == currentMessage) {
      uint8_t satelliteCount = 0;

      for (int i = 0; i < MAX_SATELLITES; ++i) {
        // copy data
        satsDisplay[i].elevation = sats[i].elevation;
        satsDisplay[i].azimuth = sats[i].azimuth;
        satsDisplay[i].snr = sats[i].snr;
        satsDisplay[i].active = sats[i].active;

        // keep track of how many satellites we can see while we're scanning through all the ID's
        // (i)
        if (satsDisplay[i].active) satelliteCount++;

        // then negate the source, so it will only be used if it's truly updated again (i.e.,
        // received again in an NMEA sat message)
        sats[i].active = false;
      }
      fixInfo.numberOfSats = satelliteCount;  // save counted satellites
    }
  }
}

void LeafGPS::testSats() {
  if (totalGPGSVMessages.isUpdated()) {
    for (int i = 0; i < 4; ++i) {
      int no = atoi(satNumber[i].value());
      // Serial.print(F("SatNumber is ")); Serial.println(no);
      if (no >= 1 && no <= MAX_SATELLITES) {
        sats[no - 1].elevation = atoi(elevation[i].value());
        sats[no - 1].azimuth = atoi(azimuth[i].value());
        sats[no - 1].snr = atoi(snr[i].value());
        sats[no - 1].active = true;
      }
    }

    int totalMessages = atoi(totalGPGSVMessages.value());
    int currentMessage = atoi(messageNumber.value());
    if (totalMessages == currentMessage) {
      /*
      // Print Sat Info
      Serial.print(F("Sats=")); Serial.print(gps.satellites.value());
      Serial.print(F(" Nums="));
      for (int i=0; i<MAX_SATELLITES; ++i)
        if (sats[i].active)
        {
          Serial.print(i+1);
          Serial.print(F(" "));
        }
      Serial.print(F(" Elevation="));
      for (int i=0; i<MAX_SATELLITES; ++i)
        if (sats[i].active)
        {
          Serial.print(sats[i].elevation);
          Serial.print(F(" "));
        }
      Serial.print(F(" Azimuth="));
      for (int i=0; i<MAX_SATELLITES; ++i)
        if (sats[i].active)
        {
          Serial.print(sats[i].azimuth);
          Serial.print(F(" "));
        }

      Serial.print(F(" SNR="));
      for (int i=0; i<MAX_SATELLITES; ++i)
        if (sats[i].active)
        {
          Serial.print(sats[i].snr);
          Serial.print(F(" "));
        }
      Serial.println();

      Serial.print("|TIME| isValid: "); Serial.print(gps.time.isValid());
      Serial.print(", isUpdated:"); Serial.print(gps.time.isUpdated());
      Serial.print(", hour: "); Serial.print(gps.time.hour());
      Serial.print(", minute: "); Serial.print(gps.time.minute());
      Serial.print(", second: "); Serial.print(gps.time.second());
      Serial.print(", age: "); Serial.print(gps.time.age());
      Serial.print(", value: "); Serial.print(gps.time.value());

      Serial.print("  |DATE| isValid: "); Serial.print(gps.date.isValid());
      Serial.print(" isUpdated: "); Serial.print(gps.date.isUpdated());
      Serial.print(" year: "); Serial.print(gps.date.year());
      Serial.print(" month: "); Serial.print(gps.date.month());
      Serial.print(" day: "); Serial.print(gps.date.day());
      Serial.print(" age: "); Serial.print(gps.date.age());
      Serial.print(" value: "); Serial.println(gps.date.value());
      */

      // Reset Active
      for (int i = 0; i < MAX_SATELLITES; ++i) {
        sats[i].active = false;
      }
    }
  }
}

bool LeafGPS::getUtcDateTime(tm& cal) {
  if (!gps.time.isValid()) {
    return false;
  }
  cal = tm{
      .tm_sec = gps.time.second(),
      .tm_min = gps.time.minute(),
      .tm_hour = gps.time.hour(),
      .tm_mday = gps.date.day(),
      .tm_mon = gps.date.month() - 1,    // tm_mon is 0-based, so subtract 1
      .tm_year = gps.date.year() - 1900  // tm_year is years since 1900
  };
  return true;
}

// like gps_getUtcDateTime, but has the timezone offset applied.
bool LeafGPS::getLocalDateTime(tm& cal) {
  if (!getUtcDateTime(cal)) {
    return false;
  }

  time_t rawTime = mktime(&cal);
  rawTime += settings.system_timeZone * 60;  // Apply the timezone offset in seconds
  cal = *localtime(&rawTime);

  return true;
}
