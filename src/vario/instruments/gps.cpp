/*
 * gps.cpp
 *
 *
 */
#include "instruments/gps.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <sys/time.h>

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

namespace {
  int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + static_cast<int>(dayOfEra) - 719468;
  }

  bool utcCalendarToEpoch(const tm& cal, time_t& epoch) {
    const int year = cal.tm_year + 1900;
    const int month = cal.tm_mon + 1;

    if (year < 1980 || month < 1 || month > 12 || cal.tm_mday < 1 || cal.tm_mday > 31 ||
        cal.tm_hour < 0 || cal.tm_hour > 23 || cal.tm_min < 0 || cal.tm_min > 59 ||
        cal.tm_sec < 0 || cal.tm_sec > 60) {
      return false;
    }

    const int64_t seconds = daysFromCivil(year, month, cal.tm_mday) * 86400LL +
                            cal.tm_hour * 3600LL + cal.tm_min * 60LL + cal.tm_sec;
    if (seconds < 0) {
      return false;
    }

    epoch = static_cast<time_t>(seconds);
    return true;
  }

  void applySystemTimeZone() {
    const int offsetMinutes = -settings.system_timeZone;
    const char sign = offsetMinutes >= 0 ? '+' : '-';
    const int absOffsetMinutes = abs(offsetMinutes);

    char timezone[16];
    snprintf(timezone, sizeof(timezone), "UTC%c%d:%02d", sign, absOffsetMinutes / 60,
             absOffsetMinutes % 60);
    setenv("TZ", timezone, 1);
    tzset();
  }

  bool isGsvSentence(const NMEAString& nmea) {
    return nmea.length() >= 7 && nmea[0] == '$' && nmea[3] == 'G' && nmea[4] == 'S' &&
           nmea[5] == 'V' && nmea[6] == ',';
  }

  bool parseNmeaIntField(const NMEAString& nmea, uint8_t targetField, int& value) {
    uint8_t field = 0;
    size_t start = nmea[0] == '$' ? 1 : 0;

    for (size_t i = start; i <= nmea.length(); ++i) {
      char c = i < nmea.length() ? nmea[i] : '\0';
      if (c == ',' || c == '*' || c == '\0') {
        if (field == targetField) {
          if (i == start) return false;

          int parsed = 0;
          for (size_t j = start; j < i; ++j) {
            if (nmea[j] < '0' || nmea[j] > '9') return false;
            parsed = parsed * 10 + (nmea[j] - '0');
          }

          value = parsed;
          return true;
        }

        ++field;
        start = i + 1;
        if (c == '*' || c == '\0') return false;
      }
    }

    return false;
  }
}  // namespace

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
  if (!baro.climbRateAverageValid()) {
    glideRatio = 0;
    return;
  }
  float climb = baro.climbRateAverage();
  float speed = gps.speed.kmph();

  if (climb == 0 || speed == 0) {
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
  syncSystemClockIfNeeded();

  // update sats if we're tracking sat NMEA sentences
  navigator.update();
  updateFixInfo();
  calculateGlideRatio();

  if (LOG::GPS && bus_) {
    String gpsName = "gps,";
    String gpsEntryString = gpsName + String(gps.location.lat(), 8) + ',' +
                            String(gps.location.lng(), 8) + ',' + String(gps.altitude.meters()) +
                            ',' + String(gps.speed.mps()) + ',' + String(gps.course.deg());

    bus_->receive(CommentMessage(gpsEntryString));
  }
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
      newSentence = gps.encode(a);
      if (newSentence) {
        // TinyGPSPlus is more forgiving than the NMEA standard by detecting a new sentence even
        // when not followed by a new line.  Encountering this block means there is a sentence
        // available, just not without the required trailing new line.  In this case, we will
        // discard the rest of the line but keep the sentence found at the beginning of the line.
        break;
      }
    }
    if (!newSentence) {
      newSentence = gps.encode('\r');
    }
  }
  if (newSentence) {
    updateSatList(msg.nmea);
    syncSystemClockIfNeeded();

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
void LeafGPS::updateSatList(const NMEAString& nmea) {
  if (!isGsvSentence(nmea)) {
    gsvSentenceGroupActive = false;
    return;
  }

  int totalMessages = 0;
  int currentMessage = 0;
  if (!parseNmeaIntField(nmea, 1, totalMessages) || !parseNmeaIntField(nmea, 2, currentMessage)) {
    return;
  }

  if (currentMessage == 1 && !gsvSentenceGroupActive) {
    for (int i = 0; i < MAX_SATELLITES; ++i) {
      sats[i].active = false;
    }
  }
  gsvSentenceGroupActive = true;

  for (int i = 0; i < 4; ++i) {
    int no = 0;
    int elevationValue = 0;
    int azimuthValue = 0;
    int snrValue = 0;
    const uint8_t field = 4 + 4 * i;

    if (parseNmeaIntField(nmea, field, no) && no >= 1 && no <= MAX_SATELLITES &&
        parseNmeaIntField(nmea, field + 1, elevationValue) &&
        parseNmeaIntField(nmea, field + 2, azimuthValue)) {
      parseNmeaIntField(nmea, field + 3, snrValue);

      sats[no - 1].elevation = elevationValue;
      sats[no - 1].azimuth = azimuthValue;
      sats[no - 1].snr = snrValue;
      sats[no - 1].active = true;
    }
  }

  // If we're on the final sentence, then copy data into the display array
  if (totalMessages == currentMessage) {
    uint8_t satelliteCount = 0;

    for (int i = 0; i < MAX_SATELLITES; ++i) {
      satsDisplay[i].elevation = sats[i].elevation;
      satsDisplay[i].azimuth = sats[i].azimuth;
      satsDisplay[i].snr = sats[i].snr;
      satsDisplay[i].active = sats[i].active;

      // keep track of how many satellites we can see while we're scanning through all the ID's
      // (i)
      if (satsDisplay[i].active) satelliteCount++;
    }
    fixInfo.numberOfSats = satelliteCount;  // save counted satellites
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
  if (!gps.date.isValid() || !gps.time.isValid()) {
    return false;
  }
  cal = tm{.tm_sec = gps.time.second(),
           .tm_min = gps.time.minute(),
           .tm_hour = gps.time.hour(),
           .tm_mday = gps.date.day(),
           .tm_mon = gps.date.month() - 1,     // tm_mon is 0-based, so subtract 1
           .tm_year = gps.date.year() - 1900,  // tm_year is years since 1900
           .tm_isdst = 0};
  return true;
}

// like gps_getUtcDateTime, but has the timezone offset applied.
bool LeafGPS::getLocalDateTime(tm& cal) {
  if (!getUtcDateTime(cal)) {
    return false;
  }

  time_t rawTime;
  if (!utcCalendarToEpoch(cal, rawTime)) {
    return false;
  }

  applySystemTimeZone();
  cal = *localtime(&rawTime);

  return true;
}

bool LeafGPS::syncSystemClock() {
  if (!hasValidDateTimeForClockSync()) {
    return false;
  }

  tm cal;
  if (!getUtcDateTime(cal)) {
    return false;
  }

  time_t rawTime;
  if (!utcCalendarToEpoch(cal, rawTime)) {
    return false;
  }

  applySystemTimeZone();
  timeval now;
  now.tv_sec = rawTime;
  now.tv_usec = 0;
  const bool synced = settimeofday(&now, nullptr) == 0;
  if (synced) {
    systemTimeSyncedThisBoot_ = true;
  }
  return synced;
}

bool LeafGPS::hasValidDateTimeForClockSync() const {
  constexpr uint32_t MAX_CLOCK_SYNC_AGE_MS = 3000;

  return gps.date.isValid() && gps.time.isValid() && gps.fixInfo.fix &&
         gps.date.age() <= MAX_CLOCK_SYNC_AGE_MS && gps.time.age() <= MAX_CLOCK_SYNC_AGE_MS;
}

void LeafGPS::syncSystemClockIfNeeded() {
  if (systemTimeSyncedThisBoot_) return;
  syncSystemClock();
}
