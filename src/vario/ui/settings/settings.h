#ifndef settings_h
#define settings_h

#include <Arduino.h>
#include <functional>

#include "comms/fanet_radio_types.h"
#include "ui/input/buttons.h"

// Types for selections
#define SETTING_LOG_FORMAT_ENTRIES 2  // How many log format entries there are
typedef uint8_t SettingLogFormat;
#define LOG_FORMAT_IGC 0
#define LOG_FORMAT_KML 1

// Setting bounds and definitions
// Vario

// Lifty Air Thermal Sniffer
#define LIFTY_AIR_MAX -8  // 0.1 m/s - sinking less than this will trigger
// Climb settings
#define CLIMB_AVERAGE_MAX 3  // units of 10 seconds, so max 30 sec averaging
#define CLIMB_START_MAX 20   // cm/s when climb note begins

// System
// Display Contrast
#define CONTRAST_MAX 20
#define CONTRAST_MIN 1
// Volume (max for both vario and system volume settings)
#define VOLUME_MAX 3
// Time Zone Offsets from UTC
#define TIME_ZONE_MIN -720  // max minutes -UTC time zone
#define TIME_ZONE_MAX 840   // max minutes +UTC time zone

// Default Settings
// Default Vario Settings
#define DEF_SINK_ALARM -2       // m/s sink
#define DEF_SINK_ALARM_UNITS 0  // 0 = m/s, 1 = fpm
#define DEF_VARIO_SENSE 3       // 3 = 1 second avg (up and down 1/4 sec from there)
#define DEF_CLIMB_AVERAGE 1     // in units of 5-seconds.  (def = 1 = 5sec)
#define DEF_CLIMB_START 5       // cm/s when climb note begins
#define DEF_VOLUME_VARIO 1      // 0=off, 1=low, 2=med, 3=high
#define DEF_QUIET_MODE 0        // 0 = off, 1 = on (ON means no beeping until flight recording)
// 0 == linear pitch interpolation; 1 == major C-scale for climb, minor scale for descent
#define DEF_VARIO_TONES 0
// In units of 10 cm/s (a sink rate of only 30cm/s means the air itself is going up).  '0' is off.
// (lift air will apply from the lifty_air setting up to the climb_start value)
#define DEF_LIFTY_AIR -4  // default -0.4m/s sink will trigger lifty air

#define DEF_ALT_SETTING 29.921  // altimeter setting
#define DEF_ALT_SYNC_GPS 0  // lock altimeter to GPS alt (to avoid local pressure setting issues)

// Default GPS & Track Log Settings
#define DEF_DISTANCE_FLOWN 0           // 0 = xc distance, 1 = path distance
#define DEF_GPS_SETTING 1              // 0 = GPS off, 1 = GPS on, 2 = power save every N sec, etc
#define DEF_TRACK_SAVE 1               // save track log?
#define DEF_AUTO_START 0               // 1 = ENABLE, 0 = DISABLE
#define DEF_AUTO_STOP 0                // 1 = ENABLE, 0 = DISABLE
#define DEF_LOG_FORMAT LOG_FORMAT_IGC  // IGC or KML

// Default System Settings

// Time Zone offset in minutes. UTC -8 (PST) would therefore be -8*60, or 480
// This allows us to cover all time zones, including the :30 minute and :15 minute ones
#define DEF_TIME_ZONE 0
#define DEF_VOLUME_SYSTEM 1  // 0=off, 1=low, 2=med, 3=high
#define DEF_ECO_MODE 0       // default off to allow reprogramming easier
#define DEF_AUTO_OFF 0       // 1 = ENABLE, 0 = DISABLE
#define DEF_WIFI_ON 0        // default wifi off
#define DEF_BLUETOOTH_ON 0   // default bluetooth off
#define DEF_SHOW_WARNING 1   // default show warning on startup

// Boot Flags
// Boot-to-ON Flag (when resetting from system updates,
// reboot to "ON" even if not holding power button)
#define DEF_BOOT_TO_ON false;
#define DEF_ENTER_BOOTLOAD false;

// Display Settings
#define DEF_CONTRAST 7  // default contrast setting
// Primary Alt field on Nav page (Baro Alt, GPS Alt, Alt above waypoint, etc)
#define DEF_NAVPG_ALT_TYP 0
#define DEF_THMPG_ALT_TYP 0   // Primary Alt field on Thermal page
#define DEF_THMPG_ALT2_TYP 0  // Secondary Alt field on Thermal page
#define DEF_THMPG_USR1 0      // User field 1 on Thermal page
#define DEF_THMPG_USR2 1      // User field 2 on Thermal page
#define DEF_SHOW_DEBUG 0      // Enable debug page
#define DEF_SHOW_THRM 1       // Enable thermal page
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

/// @brief Individual setting.
/// @tparam T Underlying data type of setting.
/// @tparam MinValue Minimum value setting may take on.
/// @tparam MaxValue Maximum value setting may take on.
/// @tparam DefaultValue Default value of setting.
template <typename T, T MinValue, T MaxValue, T DefaultValue>
class Setting {
 public:
  using ChangeHandler = std::function<void(const T&)>;

  Setting() : value_(DefaultValue) {}

  T min() { return MinValue; }
  T max() { return MaxValue; }
  T defaultValue() { return DefaultValue; }

  // Assignment operator to set the value and trigger the callback
  Setting& operator=(const T& newValue) {
    if (value_ != newValue && newValue >= MinValue && newValue <= MaxValue) {
      value_ = newValue;
      if (onChangeHandler_) {
        onChangeHandler_(value_);
      }
    }
    return *this;
  }

  // Implicit conversion to the underlying type
  operator T() const { return value_; }

  // Attach a handler for value changes
  void onChange(ChangeHandler handler) { onChangeHandler_ = handler; }

  // Increment operator (prefix)
  Setting& operator++() {
    if (value_ + 1 <= MaxValue) {
      *this = value_ + 1;
    }
    return *this;
  }

  // Increment operator (postfix)
  Setting operator++(int) {
    Setting temp = *this;
    if (value_ + 1 <= MaxValue) {
      *this = value_ + 1;
    }
    return temp;
  }

  // Decrement operator (prefix)
  Setting& operator--() {
    if (value_ - 1 >= MinValue) {
      *this = value_ - 1;
    }
    return *this;
  }

  // Decrement operator (postfix)
  Setting operator--(int) {
    Setting temp = *this;
    if (value_ - 1 >= MinValue) {
      *this = value_ - 1;
    }
    return temp;
  }

 private:
  T value_;
  ChangeHandler onChangeHandler_ = nullptr;
};

class Settings {
 public:
  // Global Variables for Current Settings
  // Vario Settings
  float vario_sinkAlarm;
  bool vario_sinkAlarm_units;
  int8_t vario_climbAvg;
  int8_t vario_climbStart;
  int8_t vario_volume;
  bool vario_quietMode;
  bool vario_tones;
  int8_t vario_liftyAir;
  float vario_altSetting;
  bool vario_altSyncToGPS;

  /* Vario Sensitivity
setting | samples | time avg
    1   |   20    | 20/20 second (1 second moving average)
    2   |   12    | 12/20 second
    3   |   6     |  6/20 second
    4   |   3     |  3/20 second
    5   |   1     |  1/20 second (single sample -- instant)
*/
  Setting<int8_t, 1, 5, 3> vario_sensitivity;

  // GPS & Track Log Settings
  bool distanceFlownType;
  int8_t gpsMode;
  bool log_saveTrack;
  bool log_autoStart;
  bool log_autoStop;
  SettingLogFormat log_format;

  // System Settings
  int16_t system_timeZone;
  int8_t system_volume;
  bool system_ecoMode;
  bool system_autoOff;
  bool system_wifiOn;
  bool system_bluetoothOn;
  bool system_showWarning;

  // Boot Flags
  bool boot_enterBootloader;
  bool boot_toOnState;

  // Display Settings
  uint8_t disp_contrast;
  uint8_t disp_navPageAltType;
  uint8_t disp_thmPageAltType;
  uint8_t disp_thmPageAlt2Type;
  uint8_t disp_thmPageUser1;
  uint8_t disp_thmPageUser2;
  bool disp_showDebugPage;
  bool disp_showThmPage;
  bool disp_showThmAdvPage;
  bool disp_showNavPage;

  // Fanet settings
  FanetRadioRegion fanet_region;
  String fanet_address;

  // Unit Values
  bool units_climb;
  bool units_alt;
  bool units_temp;
  bool units_speed;
  bool units_heading;
  bool units_distance;
  bool units_hours;

  // manage-settings functions
  void init(void);
  void loadDefaults(void);
  void save(void);
  void retrieve(void);
  void reset(void);
  void factoryResetVario(void);
  void totallyEraseNVS(void);

  // adjust-settings functions
  void adjustContrast(Button dir);
  void adjustSinkAlarm(Button dir);
  void adjustSinkAlarmUnits(bool units);
  void adjustVarioAverage(Button dir);
  void adjustClimbAverage(Button dir);
  void adjustClimbStart(Button dir);
  void adjustLiftyAir(Button dir);
  void adjustVolumeVario(Button dir);
  void adjustVolumeSystem(Button dir);
  void adjustTimeZone(Button dir);

  void adjustDisplayField_navPage_alt(Button dir);
  void adjustDisplayField_thermalPage_alt(Button dir);

  void toggleBoolNeutral(bool* boolSetting);
  void toggleBoolOnOff(bool* switchSetting);

 private:
  float sinkAlarmOptions_[2][11] = {
      {0, -1.2, -1.4, -1.6, -1.8, -2.0, -2.5, -3.0, -4.0, -5.0, -6.0},   // m/s
      {0, -240, -280, -320, -360, -400, -500, -600, -800, -1000, -1200}  // fpm
  };
};
extern Settings settings;

#endif