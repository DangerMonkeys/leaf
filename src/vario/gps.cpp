/*
 * gps.cpp
 *
 *
 */
#include "gps.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "SDcard.h"
#include "baro.h"
#include "gpx.h"
#include "log.h"
#include "message_types.h"
#include "settings.h"
#include "telemetry.h"
#include "ui/display.h"
#include "wind_estimate/wind_estimate.h"

#define DEBUG_GPS 0

// Setup GPS
#define gpsPort \
  Serial0  // This is the hardware communication port (UART0) for GPS Rx and Tx lines.  We use the
           // default ESP32S3 pins so no need to set them specifically
#define GPSBaud 115200
// #define GPSSerialBufferSize 2048
TinyGPSPlus gps;  // The TinyGPSPlus object (this is the software class that stores all the GPS info
                  // and functions)

const char enableGGA[] PROGMEM = "$PAIR062,0,1";  // enable GGA message every 1 second
const char enableGSV[] PROGMEM = "PAIR062,3,4";   // enable GSV message every 1 second
const char enableRMC[] PROGMEM = "PAIR062,4,1";   // enable RMC message every 1 second

const char disableGLL[] PROGMEM = "$PAIR062,1,0";  // disable message
const char disableGSA[] PROGMEM = "$PAIR062,2,0";  // disable message
const char disableVTG[] PROGMEM = "$PAIR062,5,0";  // disable message

uint32_t gpsBootReady = 0;

// Satellite tracking

// GPS satellite info for storing values straight from the GPS
struct gps_sat_info sats[MAX_SATELLITES];

// Cached version of the sat info for showing on display (this will be re-written each time a
// total set of new sat info is available)
struct gps_sat_info satsDisplay[MAX_SATELLITES];

// $GPGSV sentence parsing
TinyGPSCustom totalGPGSVMessages(gps, "GPGSV", 1);  // first element is # messages (N) total
TinyGPSCustom messageNumber(gps, "GPGSV", 2);       // second element is message number (x of N)
TinyGPSCustom satsInView(gps, "GPGSV", 3);          // third element is # satellites in view

// Fields for capturing the information from GSV strings
// (each GSV sentence will have info for at most 4 satellites)
TinyGPSCustom satNumber[4];  // to be initialized later
TinyGPSCustom elevation[4];  // to be initialized later
TinyGPSCustom azimuth[4];    // to be initialized later
TinyGPSCustom snr[4];        // to be initialized later

// Custom objects for position/fix accuracy.
// Need to read from the GST sentence which TinyGPS doesn't do by default
TinyGPSCustom latAccuracy(gps, "GPGST", 6);  // Latitude error - standard deviation
TinyGPSCustom lonAccuracy(gps, "GPGST", 7);  // Longitude error - standard deviation
TinyGPSCustom fix(gps, "GNGGA", 6);          // Fix (0=none, 1=GPS, 2=DGPS, 3=Valid PPS)
TinyGPSCustom fixMode(gps, "GNGSA", 2);      // Fix mode (1=No fix, 2=2D fix, 3=3D fix)
GPSFixInfo gpsFixInfo;

// Message bus to let the rest of the application know when new GPS updates are
// available
etl::imessage_bus* gps_bus = nullptr;

// Lock for GPS
SemaphoreHandle_t GpsLockGuard::mutex = NULL;

// Enable GPS Backup Power (to save satellite data and allow faster start-ups)
// This consumes a minor amount of current from the battery
// There is a loop-back pullup resistor from the backup power output to its own ENABLE line, so once
// backup is turned on, it will stay on even if the main processor is shut down. Typically, the
// backup power is only turned off to enable a full cold reboot/reset of the GPS module.
void gps_setBackupPower(bool backup_power_on) {
  if (backup_power_on)
    digitalWrite(GPS_BACKUP_EN, HIGH);
  else
    digitalWrite(GPS_BACKUP_EN, LOW);
}

// Fully reset GPS using hardware signals, including powering off backup_power to erase everything
void gps_hardReset(void) {
  gps_setBackupPower(false);
  gps_softReset();
  gps_setBackupPower(true);
}

// A soft reset, keeping backup_power enabled so as not to lose saved satellite data
void gps_softReset(void) {
  digitalWrite(GPS_RESET, LOW);
  delay(100);
  digitalWrite(GPS_RESET, HIGH);
}

void gps_enterBackupMode(void) {
  gps_setBackupPower(true);  // ensure backup power enabled (should already be on)
  // TODO: send $PAIR650,0*25 command to shutdown
  // GPS should now draw ~35uA
  // cut main VCC power to further reduce power consumption to ~13uA (i.e., when whole system is
  // shut down)
}

void gps_sleep() {
  uint32_t millisNow = millis();
  uint32_t delayTime = 0;
  if (millisNow < gpsBootReady) delayTime = gpsBootReady - millisNow;
  if (delayTime > 300) delayTime = 300;

  Serial.print("now: ");
  Serial.println(millisNow);
  Serial.print("ready: ");
  Serial.println(gpsBootReady);
  Serial.print("delay: ");
  Serial.println(delayTime);

  delay(delayTime);  // don't send a command until the GPS is booted up and ready

  gpsPort.write("$PAIR650,0*25\r\n");  // shutdown command
  // delay(100);
  Serial.println("************ !!!!!!!!!!! GPS SLEEPING COMMAND SENT !!!!!!!!!!! ************");
}

void gps_wake() {
  gps_setBackupPower(1);  // enable backup power if not already
  gps_softReset();
}

void gps_shutdown() {
  gps_sleep();
  gps_setBackupPower(
      0);  // disable GPS backup supply, so when main system shuts down, gps is totally off
}

void gps_init(void) {
  // Create the GPS Mutex for multi-threaded locking
  GpsLockGuard::mutex = xSemaphoreCreateMutex();

  // init nav class (TODO: may not need this here, just for testing at startup for ease)
  navigator.init();

  // Set pins
  Serial.print("GPS set pins... ");
  pinMode(GPS_BACKUP_EN, OUTPUT);
  gps_setBackupPower(true);  // by default, enable backup power
  pinMode(GPS_RESET, OUTPUT);
  digitalWrite(GPS_RESET, LOW);  //
  delay(100);
  digitalWrite(GPS_RESET, HIGH);  //
  gpsBootReady = millis() + 300;  // track when GPS was activated; we can't send any commands sooner
                                  // than ~285ms (we'll use 300ms)

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

float glideRatio;

void gps_calculateGlideRatio() {
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

void gps_updateFixInfo() {
  // fix status and mode
  gpsFixInfo.fix = atoi(fix.value());
  gpsFixInfo.fixMode = atoi(fixMode.value());

  // solution accuracy
  // gpsFixInfo.latError = 2.5; //atof(latAccuracy.value());
  // gpsFixInfo.lonError = 1.5; //atof(lonAccuracy.value());
  // gpsFixInfo.error = sqrt(gpsFixInfo.latError * gpsFixInfo.latError +
  //                         gpsFixInfo.lonError * gpsFixInfo.lonError);
  gpsFixInfo.error = (float)gps.hdop.value() * 5 / 100;
}

void gps_update() {
  // update sats if we're tracking sat NMEA sentences
  navigator.update();
  gps_updateSatList();
  gps_updateFixInfo();
  gps_calculateGlideRatio();

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

float gps_getGlideRatio() { return glideRatio; }

void gps_setBus(etl::imessage_bus* bus) { gps_bus = bus; }

// this is called whenever a new valid NMEA sentence contains a valid speed (TODO: check if true for
// every sentence or just every fix)
void onNewSentence(NMEASentenceContents contents) { windEstimate_onNewSentence(contents); }

bool gps_read_buffer_once() {
  GpsLockGuard mutex;  // Ensure we have a lock on write
  if (gpsPort.available()) {
    char a = gpsPort.read();
    bool newSentence = gps.encode(a);
    if (newSentence) {
      NMEASentenceContents contents = {.speed = gps.speed.isUpdated(),
                                       .course = gps.course.isUpdated()};
      // Push the update onto the bus!
      if (gps_bus && gps.location.isUpdated()) {
        gps_bus->receive(GpsReading(gps));
      }
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
void gps_updateSatList() {
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
      gpsFixInfo.numberOfSats = satelliteCount;  // save counted satellites
    }
  }
}

void gps_test_sats() {
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

bool gps_getUtcDateTime(tm& cal) {
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
bool gps_getLocalDateTime(tm& cal) {
  if (!gps_getUtcDateTime(cal)) {
    return false;
  }

  time_t rawTime = mktime(&cal);
  rawTime += TIME_ZONE * 60;  // Apply the timezone offset in seconds
  cal = *localtime(&rawTime);

  return true;
}
