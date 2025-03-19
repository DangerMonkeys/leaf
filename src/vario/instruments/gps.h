/*
 * gps.h
 *
 */

/* Notes
 PQTMPVT - quectel proprietary sentence with most(all?) of the fields we need
 or we need GGA (status, time, lat/lon, alt, sats, HDOP) and RMC (status, date, time, lat/lon,
 speed, heading) and GSV for sat info when searching

  to look into:
  RLM messages? RTCM?
  DGPS set and query status

*/

#ifndef gps_h
#define gps_h

#include <TinyGPSPlus.h>
#include "etl/message_bus.h"
#include "time.h"
#include "utils/lock_guard.h"

// GPS Satellites
#define MAX_SATELLITES 80
struct GPSSatInfo {
  bool active;
  int elevation;
  int azimuth;
  int snr;
};
struct GPSFixInfo {
  // float latError;
  // float lonError;
  float error;
  uint8_t numberOfSats;
  uint8_t fix;
  uint8_t fixMode;
};

struct NMEASentenceContents {
  bool speed;
  bool course;
};

// enum time_formats {hhmmss, }

class LeafGPS : public TinyGPSPlus {
 public:
  LeafGPS();

  void init(void);

  bool readBufferOnce(void);
  void update(void);

  // Gets a calendar time from GPS in UTC time.
  // See references such as https://en.cppreference.com/w/c/chrono/strftime
  // for how to use and format this time
  // Returns success
  bool getUtcDateTime(tm& cal);

  // like getUtcDateTime, but has the timezone offset applied.
  bool getLocalDateTime(tm& cal);

  void wake(void);
  void sleep(void);

  float getGlideRatio(void) { return glideRatio; }

  void setBus(etl::imessage_bus* bus) { bus_ = bus; }

  // Cached version of the sat info for showing on display (this will be re-written each time a
  // total set of new sat info is available)
  struct GPSSatInfo satsDisplay[MAX_SATELLITES];

  GPSFixInfo fixInfo;

 private:
  void updateFixInfo();
  void updateSatList(void);
  void setBackupPower(bool backupPowerOn);
  void enterBackupMode(void);
  void softReset(void);
  void hardReset(void);

  void shutdown(void);

  void calculateGlideRatio();

  void testSats();

  // Satellite tracking

  // GPS satellite info for storing values straight from the GPS
  struct GPSSatInfo sats[MAX_SATELLITES];

  uint32_t bootReady = 0;

  // $GPGSV sentence parsing
  TinyGPSCustom totalGPGSVMessages;  // first element is # messages (N) total
  TinyGPSCustom messageNumber;       // second element is message number (x of N)
  TinyGPSCustom satsInView;          // third element is # satellites in view

  // Fields for capturing the information from GSV strings
  // (each GSV sentence will have info for at most 4 satellites)
  TinyGPSCustom satNumber[4];  // to be initialized later
  TinyGPSCustom elevation[4];  // to be initialized later
  TinyGPSCustom azimuth[4];    // to be initialized later
  TinyGPSCustom snr[4];        // to be initialized later

  // Custom objects for position/fix accuracy.
  // Need to read from the GST sentence which TinyGPS doesn't do by default
  TinyGPSCustom latAccuracy;  // Latitude error - standard deviation
  TinyGPSCustom lonAccuracy;  // Longitude error - standard deviation
  TinyGPSCustom fix;          // Fix (0=none, 1=GPS, 2=DGPS, 3=Valid PPS)
  TinyGPSCustom fixMode;      // Fix mode (1=No fix, 2=2D fix, 3=3D fix)

  // Message bus to let the rest of the application know when new GPS updates are
  // available
  etl::imessage_bus* bus_ = nullptr;

  float glideRatio;
};
extern LeafGPS gps;

/// @brief Class to take out a SPI Mutex Lock
class GpsLockGuard : public LockGuard {
  friend void LeafGPS::init();

 public:
  GpsLockGuard() : LockGuard(mutex) {}

 private:
  static SemaphoreHandle_t mutex;
};

#endif