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
#include "lock_guard.h"
#include "time.h"

// Pinout for Leaf V3.2.0
#define GPS_BACKUP_EN \
  40  // 42 on V3.2.0  // Enable GPS backup power.  Generally always-on, except able to be turned
      // off for a full GPS reset if needed
// pins 43-44 are GPS UART, already enabled by default as "Serial0"
#define GPS_RESET 45
#define GPS_1PPS 46  // INPUT

// gps object for fix data
extern TinyGPSPlus gps;

// GPS Satellites
#define MAX_SATELLITES 80
struct gps_sat_info {
  bool active;
  int elevation;
  int azimuth;
  int snr;
};
extern struct gps_sat_info sats[MAX_SATELLITES];
extern struct gps_sat_info satsDisplay[MAX_SATELLITES];
struct GPSFixInfo {
  // float latError;
  // float lonError;
  float error;
  uint8_t numberOfSats;
  uint8_t fix;
  uint8_t fixMode;
};
extern GPSFixInfo gpsFixInfo;

struct NMEASentenceContents {
  bool speed;
  bool course;
};

// enum time_formats {hhmmss, }

void gps_init(void);
bool gps_read_buffer_once(void);
void gps_update(void);

// Gets a calendar time from GPS in UTC time.
// See references such as https://en.cppreference.com/w/c/chrono/strftime
// for how to use and format this time
// Returns success
bool gps_getUtcDateTime(tm& cal);

// like gps_getUtcDateTime, but has the timezone offset applied.
bool gps_getLocalDateTime(tm& cal);

void gps_updateSatList(void);

void gps_setBackupPower(bool backup_power_on);
void gps_enterBackupMode(void);
void gps_softReset(void);
void gps_hardReset(void);
void gps_shutdown(void);
void gps_wake(void);
void gps_sleep(void);

float gps_getGlideRatio(void);

// GPS local message bus
// This module should really be moved over to a class
extern etl::imessage_bus* gps_bus;
void gps_setBus(etl::imessage_bus* bus);

/// @brief Class to take out a SPI Mutex Lock
class GpsLockGuard : public LockGuard {
  friend void gps_init();

 public:
  GpsLockGuard() : LockGuard(mutex) {}

 private:
  static SemaphoreHandle_t mutex;
};

#endif