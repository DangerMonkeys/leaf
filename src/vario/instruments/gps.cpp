/*
 * gps.cpp
 *
 *
 */
#include "instruments/gps.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "dispatch/message_types.h"
#include "hardware/lc86g.h"
#include "instruments/baro.h"
#include "logging/log.h"
#include "logging/telemetry.h"
#include "navigation/gpx.h"
#include "storage/sd_card.h"
#include "ui/display/display.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

LeafGPS gps;

#define DEBUG_GPS 0

const char enableGGA[] PROGMEM = "$PAIR062,0,1";  // enable GGA message every 1 second
const char enableGSV[] PROGMEM = "PAIR062,3,4";   // enable GSV message every 1 second
const char enableRMC[] PROGMEM = "PAIR062,4,1";   // enable RMC message every 1 second

const char disableGLL[] PROGMEM = "$PAIR062,1,0";  // disable message
const char disableGSA[] PROGMEM = "$PAIR062,2,0";  // disable message
const char disableVTG[] PROGMEM = "$PAIR062,5,0";  // disable message

// Lock for GPS
SemaphoreHandle_t GpsLockGuard::mutex = NULL;

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

  // Initialize all the uninitialized TinyGPSCustom objects
  Serial.print("GPS initialize sat messages... ");
  for (int i = 0; i < 4; ++i) {
    satNumber[i].begin(gps, "GPGSV", 4 + 4 * i);  // offsets 4, 8, 12, 16
    elevation[i].begin(gps, "GPGSV", 5 + 4 * i);  // offsets 5, 9, 13, 17
    azimuth[i].begin(gps, "GPGSV", 6 + 4 * i);    // offsets 6, 10, 14, 18
    snr[i].begin(gps, "GPGSV", 7 + 4 * i);        // offsets 7, 11, 15, 19
  }

  Serial.print("GPS initialize done... ");
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

void LeafGPS::on_receive(const GpsMessage& msg) {
  if (DEBUG_GPS) {
    Serial.printf("LeafGPS::on_receive %d %s\n", msg.nmea.length(), msg.nmea.c_str());
  }
  bool newSentence;
  {
    GpsLockGuard mutex;  // Ensure we have a lock on write
    for (size_t i = 0; i < msg.nmea.length(); i++) {
      char a = msg.nmea[i];
      bool newSentence = gps.encode(a);
      if (newSentence) {
        fatalError("newSentence encountered in the middle of the line of text '%s'",
                   msg.nmea.c_str());
      }
    }
    newSentence = gps.encode('\r');
  }
  if (newSentence) {
    NMEASentenceContents contents = {.speed = gps.speed.isUpdated(),
                                     .course = gps.course.isUpdated()};
    // Push the parsed reading onto the bus!
    if (bus_ && gps.location.isUpdated()) {
      bus_->receive(GpsReading(gps));
    }
  } else {
    Serial.printf("NMEA sentence was not valid: %s\n", msg.nmea.c_str());
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
