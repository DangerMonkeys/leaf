#ifndef settings_h
#define settings_h

#include <Arduino.h>

#include "buttons.h"

// Types for selections
#define SETTING_LOG_FORMAT_ENTRIES 2  // How many log format entries there are
typedef uint8_t SettingLogFormat;
#define LOG_FORMAT_IGC 0
#define LOG_FORMAT_KML 1

// Setting bounds and definitions
// Vario
// Sink Alarm
#define SINK_ALARM_MAX -6     // m/s sink
#define SINK_ALARM_MIN -2

/* Vario Sensitivity
setting | samples | time avg
    1   |   30    | 1.5  second
    2   |   25    | 1.25 second
    3   |   20    | 1    second
    4   |   15    | 0.75 second
    5   |   10    | 0.5  second
*/
#define VARIO_SENSE_MAX 5  // units of 1/4 seconds
#define VARIO_SENSE_MIN 1
// Lifty Air Thermal Sniffer
#define LIFTY_AIR_MAX -8   // 0.1 m/s - sinking less than this will trigger
// Climb settings
#define CLIMB_AVERAGE_MAX 3  // units of 10 seconds, so max 30 sec averaging
#define CLIMB_START_MAX 20   // cm/s when climb note begins

// System
// Display Contrast
#define CONTRAST_MAX \
  20  // 20 steps of contrast user selectable (corresponds to actual values sent to dislpay of
      // 115-135)
#define CONTRAST_MIN 1
// Volume (max for both vario and system volume settings)
#define VOLUME_MAX 3
// Time Zone Offsets from UTC
#define TIME_ZONE_MIN -720  // max minutes -UTC time zone
#define TIME_ZONE_MAX 840   // max minutes +UTC time zone

// Default Settings
// Default Vario Settings
#define DEF_SINK_ALARM -2    // m/s sink
#define DEF_VARIO_SENSE  3   // 3 = 1 second avg (up and down 1/4 sec from there)
#define DEF_CLIMB_AVERAGE 1  // in units of 5-seconds.  (def = 1 = 5sec)
#define DEF_CLIMB_START 5    // cm/s when climb note begins
#define DEF_VOLUME_VARIO 1   // 0=off, 1=low, 2=med, 3=high
#define DEF_QUIET_MODE 0     // 0 = off, 1 = on (ON means no beeping until flight recording)
// 0 == linear pitch interpolation; 1 == major C-scale for climb, minor scale for descent
#define DEF_VARIO_TONES 0  
// In units of 10 cm/s (a sink rate of only 30cm/s means the air itself is going up).  '0' is off.
// (lift air will apply from the lifty_air setting up to the climb_start value)
#define DEF_LIFTY_AIR -4    // default -0.4m/s sink will trigger lifty air

#define DEF_ALT_SETTING 29.921  // altimeter setting
#define DEF_ALT_SYNC_GPS 0     // lock altimeter to GPS alt (to avoid local pressure setting issues)

// Default GPS & Track Log Settings
#define DEF_DISTANCE_FLOWN 0           // 0 = xc distance, 1 = path distance
#define DEF_GPS_SETTING 1              // 0 = GPS off, 1 = GPS on, 2 = power save every N sec, etc
#define DEF_TRACK_SAVE 1               // save track log?
#define DEF_AUTO_START 0               // 1 = ENABLE, 0 = DISABLE
#define DEF_AUTO_STOP 0                // 1 = ENABLE, 0 = DISABLE
#define DEF_LOG_FORMAT LOG_FORMAT_IGC  // IGC or KML

// Default System Settings
#define DEF_TIME_ZONE \
  0  // mm (in minutes) UTC -8 (PST) would therefor be -8*60, or 480.  This allows us to cover all
     // time zones, including the :30 minute and :15 minute ones
#define DEF_VOLUME_SYSTEM 1   // 0=off, 1=low, 2=med, 3=high
#define DEF_ENTER_BOOTLOAD 0  // by default, don't enter bootloader on reset
#define DEF_ECO_MODE \
  0  // default off to allow reprogramming easier.  TODO: switch to 'on' for production release
#define DEF_AUTO_OFF 0      // 1 = ENABLE, 0 = DISABLE
#define DEF_WIFI_ON 0       // default wifi off
#define DEF_BLUETOOTH_ON 0  // default bluetooth off
#define DEF_SHOW_WARNING 1      // default show warning on startup

// Display Settings
#define DEF_CONTRAST 7  // default contrast setting
#define DEF_NAVPG_ALT_TYP \
  0  // Primary Alt field on Nav page (Baro Alt, GPS Alt, Alt above waypoint, etc)
#define DEF_THMPG_ALT_TYP 0   // Primary Alt field on Thermal page
#define DEF_THMPG_ALT2_TYP 0  // Secondary Alt field on Thermal page
#define DEF_THMSPG_USR1 0     // User field 1 on Thermal simple page
#define DEF_THMSPG_USR2 1     // User field 2 on Thermal simple page
#define DEF_SHOW_DEBUG 0      // Enable debug page
#define DEF_SHOW_THRM_SIMP 1  // Enable thermal simple page
#define DEF_SHOW_THRM_ADV 0   // Enable thermal adv page
#define DEF_SHOW_NAV 1        // Enable nav page

// Default Unit Values
#define DEF_UNITS_climb 0     // 0 (m per second), 	1 (feet per minute)
#define DEF_UNITS_alt 0       // 0 (meters), 				1 (feet)
#define DEF_UNITS_temp 0      // 0 (celcius), 			1 (fahrenheit)
#define DEF_UNITS_speed 0     // 0 (kph), 				  1 (mph)
#define DEF_UNITS_heading 0   // 0 (342 deg), 		  1 (NNW)
#define DEF_UNITS_distance 0  // 0 (km, or m for <1km),	1 (miles, or ft for < 1000 feet)
#define DEF_UNITS_hours 1     // 0 (24-hour time),  1 (12 hour time),

// Global Variables for Current Settings
// Vario Settings
extern int8_t SINK_ALARM;
extern int8_t VARIO_SENSE;
extern int8_t CLIMB_AVERAGE;
extern int8_t CLIMB_START;
extern int8_t VOLUME_VARIO;
extern bool QUIET_MODE;
extern bool VARIO_TONES;
extern int8_t LIFTY_AIR;
extern float ALT_SETTING;
extern bool ALT_SYNC_GPS;

// GPS & Track Log Settings
extern bool DISTANCE_FLOWN;
extern int8_t GPS_SETTING;
extern bool TRACK_SAVE;
extern bool AUTO_START;
extern bool AUTO_STOP;
extern SettingLogFormat LOG_FORMAT;

// System Settings
extern int16_t TIME_ZONE;
extern int8_t VOLUME_SYSTEM;
extern bool ENTER_BOOTLOAD;
extern bool ECO_MODE;
extern bool AUTO_OFF;
extern bool WIFI_ON;
extern bool BLUETOOTH_ON;
extern bool SHOW_WARNING;

// Display Settings
extern uint8_t CONTRAST;
extern uint8_t NAVPG_ALT_TYP;
extern uint8_t THMPG_ALT_TYP;
extern uint8_t THMPG_ALT2_TYP;
extern uint8_t THMSPG_USR1;
extern uint8_t THMSPG_USR2;
extern bool SHOW_DEBUG;
extern bool SHOW_THRM_SIMP;
extern bool SHOW_THRM_ADV;
extern bool SHOW_NAV;

// Unit Values
extern bool UNITS_climb;
extern bool UNITS_alt;
extern bool UNITS_temp;
extern bool UNITS_speed;
extern bool UNITS_heading;
extern bool UNITS_distance;
extern bool UNITS_hours;

// manage-settings functions
void settings_init(void);
void settings_loadDefaults(void);
void settings_reset(void);
void settings_save(void);
void settings_retrieve(void);
void factoryResetVario(void);

// adjust-settings functions
void settings_adjustContrast(Button dir);
void settings_setAltOffset(int32_t value);
void settings_adjustAltOffset(Button dir, uint8_t count);
bool settings_matchGPSAlt(void);
void settings_adjustSinkAlarm(Button dir);
void settings_adjustVarioAverage(Button dir);
void settings_adjustClimbAverage(Button dir);
void settings_adjustClimbStart(Button dir);
void settings_adjustLiftyAir(Button dir);
void settings_adjustVolumeVario(Button dir);
void settings_adjustVolumeSystem(Button dir);
void settings_adjustTimeZone(Button dir);

void settings_adjustDisplayField_navPage_alt(Button dir);
void settings_adjustDisplayField_thermalPage_alt(Button dir);

void settings_toggleBoolNeutral(bool* boolSetting);
void settings_toggleBoolOnOff(bool* switchSetting);

/*
// Timer/Stopwatch Variable
extern unsigned long TIMER;				// GLOBAL
extern unsigned char TIMER_RUNNING;		// GLOBAL


// Waypoint Input and Edit Variables
extern char WYPT_tempName[];			// 8 char string name plus end character
extern signed char WYPT_tempAltSign;
extern unsigned long WYPT_tempAlt;
extern unsigned char WYPT_tempAltUnits;
extern signed char WYPT_tempLatSign;
extern unsigned long WYPT_tempLatDeg;
extern unsigned long WYPT_tempLatFrac;
extern signed char WYPT_tempLongSign;
extern unsigned long WYPT_tempLongDeg;
extern unsigned long WYPT_tempLongFrac;
extern unsigned char WYPT_editCursor;

enum settings_units {units_alt, units_climb, units_speed, units_distance, units_heading, units_temp,
units_hours};

// Alt offset




void settings_init(void);
void settings_adjustContrast(signed char dir);

void settings_resetToDefaults(void);
void settings_readSettings(void);
void settings_saveSettings(void);

void settings_gps_cycle(void);

void settings_altOffset(signed char dir, unsigned int count, signed long value);

void settings_sinkAlarm_cycle(void);
void settings_varioAverage_cycle(void);
void settings_climbAverage_cycle(void);
void settings_volume_cycle(void);
void settings_liftyAir_cycle(void);
void settings_track_cycle(void);


void settings_units_cycle(unsigned char whichUnits);
void settings_timeOffset(signed char dir, unsigned int count);
void settings_timerAutoStart_cycle(void);
void settings_powerAutoOff_cycle(void);

unsigned char settings_cycle_1and0(unsigned char input);

//waypoint menu/subpages
void settings_waypointEnter(signed char enteredFrom);		// get into the waypoint sub-screens
void settings_waypointExit(void);							// leave
waypoint sub-screens void settings_waypointSelect(void);
// handle a selected waypoint void settings_waypointListScroll(signed char dir);
// scroll the waypoint list

// editing waypoints
void settings_waypointEdit_shift(void);
void settings_waypointEdit_inc(signed char dir);
signed long settings_cycleDigit(signed long val, char place, signed char dir);





// Default Settings on First Boot Up

  // Pargliding Vario




  // Off Roading
    uint8_t rollover_max_fore
    uint8_t rollover_max_rear
    uint8_t rollover_max_left
    uint8_t rollover_max_right
    uint8_t rollover_warning        // degrees before max to receive warnings



  // Aircraft
    //


  // Data Logger
    //



    //


*/

#endif