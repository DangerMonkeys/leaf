

#include "settings.h"

#include <Preferences.h>
#include <nvs_flash.h>

#include "baro.h"
#include "gps.h"
#include "speaker.h"
#include "ui/display.h"

#define RW_MODE false
#define RO_MODE true

// Global Variables for Current Settings
// Vario Settings
int8_t VOLUME_VARIO;
bool QUIET_MODE;
bool VARIO_TONES;
int8_t VARIO_SENSE;
int8_t CLIMB_AVERAGE;
int8_t CLIMB_START;
int8_t LIFTY_AIR;
int8_t SINK_ALARM;
float ALT_SETTING;
bool ALT_SYNC_GPS;

// GPS & Track Log Settings
bool DISTANCE_FLOWN;
int8_t GPS_SETTING;
bool TRACK_SAVE;
bool AUTO_START;
bool AUTO_STOP;
SettingLogFormat LOG_FORMAT;

// System Settings
int16_t TIME_ZONE;
int8_t VOLUME_SYSTEM;
bool ECO_MODE;
bool AUTO_OFF;
bool WIFI_ON;
bool BLUETOOTH_ON;
bool SHOW_WARNING;

// Boot Flags
bool ENTER_BOOTLOAD;
bool BOOT_TO_ON;

// Display Settings
uint8_t CONTRAST;
uint8_t NAVPG_ALT_TYP;
uint8_t THMPG_ALT_TYP;
uint8_t THMPG_ALT2_TYP;
uint8_t THMSPG_USR1;
uint8_t THMSPG_USR2;
bool SHOW_DEBUG;
bool SHOW_THRM_SIMP;
bool SHOW_THRM_ADV;
bool SHOW_NAV;

// Fanet settings
FanetRadioRegion FANET_region;
String FANET_address;

// Unit Values
bool UNITS_climb;
bool UNITS_alt;
bool UNITS_temp;
bool UNITS_speed;
bool UNITS_heading;
bool UNITS_distance;
bool UNITS_hours;

Preferences leafPrefs;

void settings_init() {
  settings_loadDefaults();  // load defaults regardless, but we'll overwrite these with saved user
                            // settings (if available)

  // Check if settings have been saved before (or not), then save defaults (or grab saved settings)
  leafPrefs.begin("varioPrefs", RO_MODE);  // open (or create if needed) the varioPrefs namespace
                                           // for user settings/preferences
  bool newBootupVario = !leafPrefs.isKey(
      "nvsInitVario");  // check if we've ever initialized the non volatile storage (nvs), or if
                        // this is a new device boot up for the first time
  // new bootup into Vario
  if (newBootupVario) {
    leafPrefs.end();
    leafPrefs.begin("varioPrefs", RW_MODE);
    settings_save();  // save defaults to NVS0
    leafPrefs.end();
  } else {
    settings_retrieve();
  }
}

void factoryResetVario() {
  leafPrefs.remove(
      "nvsInitVario");  // remove this key so that we force a factory settings reload on next reboot
}

void settings_loadDefaults() {
  // Vario Settings
  SINK_ALARM = DEF_SINK_ALARM;
  VARIO_SENSE = DEF_VARIO_SENSE;
  CLIMB_AVERAGE = DEF_CLIMB_AVERAGE;
  CLIMB_START = DEF_CLIMB_START;
  VOLUME_VARIO = DEF_VOLUME_VARIO;
  QUIET_MODE = DEF_QUIET_MODE;
  VARIO_TONES = DEF_VARIO_TONES;
  LIFTY_AIR = DEF_LIFTY_AIR;
  ALT_SETTING = DEF_ALT_SETTING;
  ALT_SYNC_GPS = DEF_ALT_SYNC_GPS;

  // GPS & Track Log Settings
  DISTANCE_FLOWN = DEF_DISTANCE_FLOWN;
  GPS_SETTING = DEF_GPS_SETTING;
  TRACK_SAVE = DEF_TRACK_SAVE;
  AUTO_START = DEF_AUTO_START;
  AUTO_STOP = DEF_AUTO_STOP;
  LOG_FORMAT = DEF_LOG_FORMAT;

  // System Settings
  TIME_ZONE = DEF_TIME_ZONE;
  VOLUME_SYSTEM = DEF_VOLUME_SYSTEM;
  ECO_MODE = DEF_ECO_MODE;
  AUTO_OFF = DEF_AUTO_OFF;
  WIFI_ON = DEF_WIFI_ON;
  BLUETOOTH_ON = DEF_BLUETOOTH_ON;
  SHOW_WARNING = DEF_SHOW_WARNING;

  // Boot Flags
  ENTER_BOOTLOAD = DEF_ENTER_BOOTLOAD;
  BOOT_TO_ON = DEF_BOOT_TO_ON;

  // Display Settings
  CONTRAST = DEF_CONTRAST;
  NAVPG_ALT_TYP = DEF_NAVPG_ALT_TYP;
  THMPG_ALT_TYP = DEF_THMPG_ALT_TYP;
  THMPG_ALT2_TYP = DEF_THMPG_ALT2_TYP;
  THMSPG_USR1 = DEF_THMSPG_USR1;
  THMSPG_USR2 = DEF_THMSPG_USR2;
  SHOW_DEBUG = DEF_SHOW_DEBUG;
  SHOW_THRM_SIMP = DEF_SHOW_THRM_SIMP;
  SHOW_THRM_ADV = DEF_SHOW_THRM_ADV;
  SHOW_NAV = DEF_SHOW_NAV;

  // Unit Values
  UNITS_climb = DEF_UNITS_climb;
  UNITS_alt = DEF_UNITS_alt;
  UNITS_temp = DEF_UNITS_temp;
  UNITS_speed = DEF_UNITS_speed;
  UNITS_heading = DEF_UNITS_heading;
  UNITS_distance = DEF_UNITS_distance;
  UNITS_hours = DEF_UNITS_hours;
}

void settings_retrieve() {
  leafPrefs.begin("varioPrefs", RO_MODE);

  // Vario Settings
  SINK_ALARM = leafPrefs.getChar("SINK_ALARM");
  VARIO_SENSE = leafPrefs.getChar("VARIO_SENSE");
  CLIMB_AVERAGE = leafPrefs.getChar("CLIMB_AVERAGE");
  CLIMB_START = leafPrefs.getChar("CLIMB_START");
  VOLUME_VARIO = leafPrefs.getChar("VOLUME_VARIO");
  QUIET_MODE = leafPrefs.getBool("QUIET_MODE");
  VARIO_TONES = leafPrefs.getBool("VARIO_TONES");
  LIFTY_AIR = leafPrefs.getChar("LIFTY_AIR");
  ALT_SETTING = leafPrefs.getFloat("ALT_SETTING");
  ALT_SYNC_GPS = leafPrefs.getBool("ALT_SYNC_GPS");

  // GPS & Track Log Settings
  DISTANCE_FLOWN = leafPrefs.getBool("DISTANCE_FLOWN");
  GPS_SETTING = leafPrefs.getChar("GPS_SETTING");
  TRACK_SAVE = leafPrefs.getBool("TRACK_SAVE");
  AUTO_START = leafPrefs.getBool("AUTO_START");
  LOG_FORMAT = leafPrefs.getUChar("LOG_FORMAT");

  // System Settings
  TIME_ZONE = leafPrefs.getShort("TIME_ZONE");
  VOLUME_SYSTEM = leafPrefs.getChar("VOLUME_SYSTEM");
  ECO_MODE = leafPrefs.getBool("ECO_MODE");
  AUTO_OFF = leafPrefs.getBool("AUTO_OFF");
  WIFI_ON = leafPrefs.getBool("WIFI_ON");
  BLUETOOTH_ON = leafPrefs.getBool("BLUETOOTH_ON");
  SHOW_WARNING = leafPrefs.getBool("SHOW_WARNING");

  // Boot Flags
  ENTER_BOOTLOAD = leafPrefs.getBool("ENTER_BOOTLOAD");
  BOOT_TO_ON = leafPrefs.getBool("BOOT_TO_ON");

  // Display Settings
  CONTRAST = leafPrefs.getUChar("CONTRAST");
  if (CONTRAST < CONTRAST_MIN || CONTRAST > CONTRAST_MAX) CONTRAST = DEF_CONTRAST;
  NAVPG_ALT_TYP = leafPrefs.getUChar("NAVPG_ALT_TYP");
  THMPG_ALT_TYP = leafPrefs.getUChar("THMPG_ALT_TYP");
  THMPG_ALT2_TYP = leafPrefs.getUChar("THMPG_ALT2_TYP");
  THMSPG_USR1 = leafPrefs.getUChar("THMSPG_USR1");
  THMSPG_USR2 = leafPrefs.getUChar("THMSPG_USR2");
  SHOW_DEBUG = leafPrefs.getBool("SHOW_DEBUG");
  SHOW_THRM_SIMP = leafPrefs.getBool("SHOW_THRM_SIMP");
  SHOW_THRM_ADV = leafPrefs.getBool("SHOW_THRM_ADV");
  SHOW_NAV = leafPrefs.getBool("SHOW_NAV");

  // Fanet settings
  FANET_region = (FanetRadioRegion)leafPrefs.getUInt("FANET_REGION");
  FANET_address = leafPrefs.getString("FANET_ADDRESS");

  // Unit Values
  UNITS_climb = leafPrefs.getBool("UNITS_climb");
  UNITS_alt = leafPrefs.getBool("UNITS_alt");
  UNITS_temp = leafPrefs.getBool("UNITS_temp");
  UNITS_speed = leafPrefs.getBool("UNITS_speed");
  UNITS_heading = leafPrefs.getBool("UNITS_heading");
  UNITS_distance = leafPrefs.getBool("UNITS_distance");
  UNITS_hours = leafPrefs.getBool("UNITS_hours");

  leafPrefs.end();
}

void settings_save() {
  // Save settings before shutdown (or other times as needed)

  leafPrefs.begin("varioPrefs", RW_MODE);

  // save flag to indicate we have previously initialized NVS storage and have saved settings
  // available
  leafPrefs.putBool("nvsInitVario", true);

  // Vario Settings
  leafPrefs.putChar("SINK_ALARM", SINK_ALARM);
  leafPrefs.putChar("VARIO_SENSE", VARIO_SENSE);
  leafPrefs.putChar("CLIMB_AVERAGE", CLIMB_AVERAGE);
  leafPrefs.putChar("CLIMB_START", CLIMB_START);
  leafPrefs.putChar("VOLUME_VARIO", VOLUME_VARIO);
  leafPrefs.putBool("QUIET_MODE", QUIET_MODE);
  leafPrefs.putBool("VARIO_TONES", VARIO_TONES);
  leafPrefs.putChar("LIFTY_AIR", LIFTY_AIR);
  leafPrefs.putFloat("ALT_SETTING", ALT_SETTING);
  leafPrefs.putBool("ALT_SYNC_GPS", ALT_SYNC_GPS);
  // GPS & Track Log Settings
  leafPrefs.putBool("DISTANCE_FLOWN", DISTANCE_FLOWN);
  leafPrefs.putChar("GPS_SETTING", GPS_SETTING);
  leafPrefs.putBool("TRACK_SAVE", TRACK_SAVE);
  leafPrefs.putBool("AUTO_START", AUTO_START);
  leafPrefs.putBool("AUTO_STOP", AUTO_STOP);
  leafPrefs.putUChar("LOG_FORMAT", LOG_FORMAT);
  // System Settings
  leafPrefs.putShort("TIME_ZONE", TIME_ZONE);
  leafPrefs.putChar("VOLUME_SYSTEM", VOLUME_SYSTEM);
  leafPrefs.putBool("ECO_MODE", ECO_MODE);
  leafPrefs.putBool("AUTO_OFF", AUTO_OFF);
  leafPrefs.putBool("WIFI_ON", WIFI_ON);
  leafPrefs.putBool("BLUETOOTH_ON", BLUETOOTH_ON);
  leafPrefs.putBool("SHOW_WARNING", SHOW_WARNING);
  // Boot Flags
  leafPrefs.putBool("ENTER_BOOTLOAD", ENTER_BOOTLOAD);
  leafPrefs.putBool("BOOT_TO_ON", BOOT_TO_ON);
  // Display Settings
  leafPrefs.putUChar("CONTRAST", CONTRAST);
  leafPrefs.putUChar("NAVPG_ALT_TYP", NAVPG_ALT_TYP);
  leafPrefs.putUChar("THMPG_ALT_TYP", THMPG_ALT_TYP);
  leafPrefs.putUChar("THMPG_ALT2_TYP", THMPG_ALT2_TYP);
  leafPrefs.putUChar("THMSPG_USR1", THMSPG_USR1);
  leafPrefs.putUChar("THMSPG_USR2", THMSPG_USR2);
  leafPrefs.putBool("SHOW_DEBUG", SHOW_DEBUG);
  leafPrefs.putBool("SHOW_THRM_SIMP", SHOW_THRM_SIMP);
  leafPrefs.putBool("SHOW_THRM_ADV", SHOW_THRM_ADV);
  leafPrefs.putBool("SHOW_NAV", SHOW_NAV);
  // Fanet Settings
  leafPrefs.putUInt("FANET_REGION", (uint32_t)FANET_region);
  leafPrefs.putString("FANET_ADDRESS", FANET_address);
  // Unit Values
  leafPrefs.putBool("UNITS_climb", UNITS_climb);
  leafPrefs.putBool("UNITS_alt", UNITS_alt);
  leafPrefs.putBool("UNITS_temp", UNITS_temp);
  leafPrefs.putBool("UNITS_speed", UNITS_speed);
  leafPrefs.putBool("UNITS_heading", UNITS_heading);
  leafPrefs.putBool("UNITS_distance", UNITS_distance);
  leafPrefs.putBool("UNITS_hours", UNITS_hours);

  leafPrefs.end();
}

void settings_reset() {
  settings_loadDefaults();
  settings_save();
}

// we probably should never have to call this
void settings_totallyEraseNVS() {
  nvs_flash_erase();  // erase the NVS partition and...
  nvs_flash_init();   // initialize the NVS partition.
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adjust individual settings

// Contrast Adjustment
void settings_adjustContrast(Button dir) {
  uint16_t* sound = fx_neutral;
  if (dir == Button::RIGHT)
    sound = fx_increase;
  else if (dir == Button::LEFT)
    sound = fx_decrease;
  else if (dir == Button::CENTER) {  // reset to default
    speaker_playSound(fx_confirm);
    CONTRAST = DEF_CONTRAST;
    display_setContrast(CONTRAST);
    return;
  }

  CONTRAST += dir == Button::RIGHT ? 1 : -1;

  if (CONTRAST > CONTRAST_MAX) {
    CONTRAST = CONTRAST_MAX;
    sound = fx_double;
  } else if (CONTRAST < CONTRAST_MIN) {
    CONTRAST = CONTRAST_MIN;
    sound = fx_double;
  }
  display_setContrast(CONTRAST);
  speaker_playSound(sound);
}

// Alt Offsets

// solve for the altimeter setting required to make corrected-pressure-altitude match gps-altitude
bool settings_matchGPSAlt() {
  bool success = false;
  if (gps.altitude.isValid()) {
    baro.altimeterSetting =
        baro.pressure / (3386.389 * pow(1 - gps.altitude.meters() * 100 / 4433100.0, 1 / 0.190264));
    ALT_SETTING = baro.altimeterSetting;
    success = true;
  }
  return success;
}

void settings_adjustSinkAlarm(Button dir) {
  uint16_t* sound = fx_neutral;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    if (++SINK_ALARM > 0) {
      SINK_ALARM =
          SINK_ALARM_MAX;  // if we were at 0 and now are at positive 1, go back to max sink rate
    } else if (SINK_ALARM > SINK_ALARM_MIN) {
      sound = fx_cancel;
      SINK_ALARM = 0;  // if we were at MIN (say, -2), jump to 0 (off)
    }
  } else {
    sound = fx_decrease;
    if (--SINK_ALARM < SINK_ALARM_MAX) {
      sound = fx_cancel;
      SINK_ALARM = 0;  // if we were at max, wrap back to 0
    } else if (SINK_ALARM > SINK_ALARM_MIN) {
      SINK_ALARM = SINK_ALARM_MIN;  // if we were at 0, and dropped to -1, but still greater than
                                    // the min (-2), jump to -2
    }
  }
  speaker_playSound(sound);
  // TODO: really needed? speaker_updateClimbToneParameters();	// call to adjust sinkRateSpread
  // according to new SINK_ALARM value
}

void settings_adjustVarioAverage(Button dir) {
  uint16_t* sound = fx_neutral;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    if (++VARIO_SENSE >= VARIO_SENSE_MAX) {
      VARIO_SENSE = VARIO_SENSE_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    if (--VARIO_SENSE <= VARIO_SENSE_MIN) {
      VARIO_SENSE = VARIO_SENSE_MIN;
      sound = fx_double;
    }
  }
  speaker_playSound(sound);
}

// climb average goes between 0 and CLIMB_AVERAGE_MAX
void settings_adjustClimbAverage(Button dir) {
  uint16_t* sound = fx_neutral;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    if (++CLIMB_AVERAGE >= CLIMB_AVERAGE_MAX) {
      CLIMB_AVERAGE = CLIMB_AVERAGE_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    if (--CLIMB_AVERAGE <= 0) {
      CLIMB_AVERAGE = 0;
      sound = fx_double;
    }
  }
  speaker_playSound(sound);
}

void settings_adjustClimbStart(Button dir) {
  uint16_t* sound = fx_neutral;
  uint8_t inc_size = 5;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    if ((CLIMB_START += inc_size) >= CLIMB_START_MAX) {
      CLIMB_START = CLIMB_START_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    if ((CLIMB_START -= inc_size) <= 0) {
      CLIMB_START = 0;
      sound = fx_double;
    }
  }
  speaker_playSound(sound);
}

void settings_adjustLiftyAir(Button dir) {
  uint16_t* sound = fx_neutral;

  // adjust the setting based on button direction
  if (dir == Button::RIGHT) {
    LIFTY_AIR += 1;
    sound = fx_increase;
  } else {
    LIFTY_AIR += -1;
    sound = fx_decrease;
  }

  // now scrub the result to ensure we're within bounds
  // if we were at 0 and now are at positive 1, go back to max sink setting
  if (LIFTY_AIR > 0) {
    LIFTY_AIR = LIFTY_AIR_MAX;
    sound = fx_increase;
  } else if (LIFTY_AIR == 0) {  // setting to 0 turns the feature off
    sound = fx_cancel;
  } else if (LIFTY_AIR < LIFTY_AIR_MAX) {  // wrap from max back to 0
    sound = fx_cancel;
    LIFTY_AIR = 0;
  }
  speaker_playSound(sound);
}

void settings_adjustVolumeVario(Button dir) {
  uint16_t* sound = fx_neutral;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    VOLUME_VARIO++;
    if (VOLUME_VARIO > VOLUME_MAX) {
      VOLUME_VARIO = VOLUME_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    VOLUME_VARIO--;
    if (VOLUME_VARIO <= 0) {
      VOLUME_VARIO = 0;
      sound = fx_cancel;  // even if vario volume is set to 0, the system volume may still be turned
                          // on, so we have a sound for turning vario off
    }
  }
  speaker_playSound(sound);
}

void settings_adjustVolumeSystem(Button dir) {
  uint16_t* sound = fx_neutral;
  if (dir == Button::RIGHT) {
    sound = fx_increase;
    VOLUME_SYSTEM++;
    if (VOLUME_SYSTEM > VOLUME_MAX) {
      VOLUME_SYSTEM = VOLUME_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    VOLUME_SYSTEM--;
    if (VOLUME_SYSTEM <= 0) {
      VOLUME_SYSTEM = 0;
      sound = fx_cancel;  // we have this line of code for completeness, but the speaker will be
                          // turned off for system sounds so you won't hear it
    }
  }
  speaker_playSound(sound);
}

uint8_t timeZoneIncrement =
    60;  // in minutes.  This allows us to change and adjust by 15 minutes for some regions that
         // have half-hour and quarter-hour time zones.
void settings_adjustTimeZone(Button dir) {
  if (dir == Button::CENTER) {  // switch from half-hour to full-hour increments
    if (timeZoneIncrement == 60) {
      timeZoneIncrement = 15;
      speaker_playSound(fx_increase);
    } else if (timeZoneIncrement == 15) {
      timeZoneIncrement = 60;
      speaker_playSound(fx_decrease);
    }
  }
  if (dir == Button::RIGHT)
    if (TIME_ZONE >= TIME_ZONE_MAX) {
      speaker_playSound(fx_double);
      TIME_ZONE = TIME_ZONE_MAX;
    } else {
      TIME_ZONE += timeZoneIncrement;
      speaker_playSound(fx_neutral);
    }
  else if (dir == Button::LEFT) {
    if (TIME_ZONE <= TIME_ZONE_MIN) {
      speaker_playSound(fx_double);
      TIME_ZONE = TIME_ZONE_MIN;
    } else {
      TIME_ZONE -= timeZoneIncrement;
      speaker_playSound(fx_neutral);
    }
  }
}

// Change which altitude is shown on the Nav page (Baro Alt, GPS Alt, or Above-Waypoint Alt)
void settings_adjustDisplayField_navPage_alt(Button dir) {
  if (dir == Button::RIGHT) {
    NAVPG_ALT_TYP++;
    if (NAVPG_ALT_TYP >= 3) NAVPG_ALT_TYP = 0;
  } else {
    if (NAVPG_ALT_TYP == 0)
      NAVPG_ALT_TYP = 1;
    else
      NAVPG_ALT_TYP--;
  }
  speaker_playSound(fx_neutral);
}

// Change which altitude is shown on the Thermal page (Baro Alt or GPS Alt)
void settings_adjustDisplayField_thermalPage_alt(Button dir) {
  if (dir == Button::RIGHT) {
    THMPG_ALT_TYP++;
    if (THMPG_ALT_TYP >= 2) THMPG_ALT_TYP = 0;
  } else {
    if (THMPG_ALT_TYP == 0)
      THMPG_ALT_TYP = 1;
    else
      THMPG_ALT_TYP--;
  }
  speaker_playSound(fx_neutral);
}

// swap unit settings and play a neutral sound
void settings_toggleBoolNeutral(bool* unitSetting) {
  *unitSetting = !*unitSetting;
  speaker_playSound(fx_neutral);
}

// flip on/off certain settings and play on/off sounds
void settings_toggleBoolOnOff(bool* switchSetting) {
  *switchSetting = !*switchSetting;
  if (*switchSetting)
    speaker_playSound(fx_enter);  // if we turned it on
  else
    speaker_playSound(fx_cancel);  // if we turned it off
}
