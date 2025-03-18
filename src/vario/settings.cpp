

#include "settings.h"

#include <Preferences.h>
#include <nvs_flash.h>

#include "baro.h"
#include "gps.h"
#include "speaker.h"
#include "ui/display.h"

#define RW_MODE false
#define RO_MODE true

Settings settings;

Preferences leafPrefs;

void Settings::init() {
  loadDefaults();  // load defaults regardless, but we'll overwrite these with saved user
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
    save();  // save defaults to NVS0
    leafPrefs.end();
  } else {
    retrieve();
  }
}

void Settings::factoryResetVario() {
  leafPrefs.remove(
      "nvsInitVario");  // remove this key so that we force a factory settings reload on next reboot
}

void Settings::loadDefaults() {
  // Vario Settings
  vario_sinkAlarm = DEF_SINK_ALARM;
  vario_sensitivity = DEF_VARIO_SENSE;
  vario_climbAvg = DEF_CLIMB_AVERAGE;
  vario_climbStart = DEF_CLIMB_START;
  vario_volume = DEF_VOLUME_VARIO;
  vario_quietMode = DEF_QUIET_MODE;
  vario_tones = DEF_VARIO_TONES;
  vario_liftyAir = DEF_LIFTY_AIR;
  vario_altSetting = DEF_ALT_SETTING;
  vario_altSyncToGPS = DEF_ALT_SYNC_GPS;

  // GPS & Track Log Settings
  distanceFlownType = DEF_DISTANCE_FLOWN;
  gpsMode = DEF_GPS_SETTING;
  log_saveTrack = DEF_TRACK_SAVE;
  log_autoStart = DEF_AUTO_START;
  log_autoStop = DEF_AUTO_STOP;
  log_format = DEF_LOG_FORMAT;

  // System Settings
  system_timeZone = DEF_TIME_ZONE;
  system_volume = DEF_VOLUME_SYSTEM;
  system_ecoMode = DEF_ECO_MODE;
  system_autoOff = DEF_AUTO_OFF;
  system_wifiOn = DEF_WIFI_ON;
  system_bluetoothOn = DEF_BLUETOOTH_ON;
  system_showWarning = DEF_SHOW_WARNING;

  // Boot Flags
  boot_enterBootloader = DEF_ENTER_BOOTLOAD;
  boot_toOnState = DEF_BOOT_TO_ON;

  // Display Settings
  disp_contrast = DEF_CONTRAST;
  disp_navPageAltType = DEF_NAVPG_ALT_TYP;
  disp_thmPageAltType = DEF_THMPG_ALT_TYP;
  disp_thmPageAlt2Type = DEF_THMPG_ALT2_TYP;
  disp_thmPageUser1 = DEF_THMPG_USR1;
  disp_thmPageUser2 = DEF_THMPG_USR2;
  disp_showDebugPage = DEF_SHOW_DEBUG;
  disp_showThmPage = DEF_SHOW_THRM;
  disp_showThmAdvPage = DEF_SHOW_THRM_ADV;
  disp_showNavPage = DEF_SHOW_NAV;

  // Unit Values
  units_climb = DEF_UNITS_climb;
  units_alt = DEF_UNITS_alt;
  units_temp = DEF_UNITS_temp;
  units_speed = DEF_UNITS_speed;
  units_heading = DEF_UNITS_heading;
  units_distance = DEF_UNITS_distance;
  units_hours = DEF_UNITS_hours;
}

void Settings::retrieve() {
  leafPrefs.begin("varioPrefs", RO_MODE);

  // Vario Settings
  vario_sinkAlarm = leafPrefs.getChar("SINK_ALARM");
  vario_sensitivity = leafPrefs.getChar("vario_sensitivity");
  vario_climbAvg = leafPrefs.getChar("CLIMB_AVERAGE");
  vario_climbStart = leafPrefs.getChar("CLIMB_START");
  vario_volume = leafPrefs.getChar("VOLUME_VARIO");
  vario_quietMode = leafPrefs.getBool("QUIET_MODE");
  vario_tones = leafPrefs.getBool("VARIO_TONES");
  vario_liftyAir = leafPrefs.getChar("LIFTY_AIR");
  vario_altSetting = leafPrefs.getFloat("ALT_SETTING");
  vario_altSyncToGPS = leafPrefs.getBool("ALT_SYNC_GPS");

  // GPS & Track Log Settings
  distanceFlownType = leafPrefs.getBool("DISTANCE_FLOWN");
  gpsMode = leafPrefs.getChar("GPS_SETTING");
  log_saveTrack = leafPrefs.getBool("TRACK_SAVE");
  log_autoStart = leafPrefs.getBool("AUTO_START");
  log_format = leafPrefs.getUChar("LOG_FORMAT");

  // System Settings
  system_timeZone = leafPrefs.getShort("TIME_ZONE");
  system_volume = leafPrefs.getChar("VOLUME_SYSTEM");
  system_ecoMode = leafPrefs.getBool("ECO_MODE");
  system_autoOff = leafPrefs.getBool("AUTO_OFF");
  system_wifiOn = leafPrefs.getBool("WIFI_ON");
  system_bluetoothOn = leafPrefs.getBool("BLUETOOTH_ON");
  system_showWarning = leafPrefs.getBool("SHOW_WARNING");

  // Boot Flags
  boot_enterBootloader = leafPrefs.getBool("ENTER_BOOTLOAD");
  boot_toOnState = leafPrefs.getBool("BOOT_TO_ON");

  // Display Settings
  disp_contrast = leafPrefs.getUChar("CONTRAST");
  if (disp_contrast < CONTRAST_MIN || disp_contrast > CONTRAST_MAX) disp_contrast = DEF_CONTRAST;
  disp_navPageAltType = leafPrefs.getUChar("NAVPG_ALT_TYP");
  disp_thmPageAltType = leafPrefs.getUChar("THMPG_ALT_TYP");
  disp_thmPageAlt2Type = leafPrefs.getUChar("THMPG_ALT2_TYP");
  disp_thmPageUser1 = leafPrefs.getUChar("THMPG_USR1");
  disp_thmPageUser2 = leafPrefs.getUChar("THMPG_USR2");
  disp_showDebugPage = leafPrefs.getBool("SHOW_DEBUG");
  disp_showThmPage = leafPrefs.getBool("SHOW_THRM");
  disp_showThmAdvPage = leafPrefs.getBool("SHOW_THRM_ADV");
  disp_showNavPage = leafPrefs.getBool("SHOW_NAV");

  // Fanet settings
  fanet_region = (FanetRadioRegion)leafPrefs.getUInt("FANET_REGION");
  fanet_address = leafPrefs.getString("FANET_ADDRESS");

  // Unit Values
  units_climb = leafPrefs.getBool("UNITS_climb");
  units_alt = leafPrefs.getBool("UNITS_alt");
  units_temp = leafPrefs.getBool("UNITS_temp");
  units_speed = leafPrefs.getBool("UNITS_speed");
  units_heading = leafPrefs.getBool("UNITS_heading");
  units_distance = leafPrefs.getBool("UNITS_distance");
  units_hours = leafPrefs.getBool("UNITS_hours");

  leafPrefs.end();
}

void Settings::save() {
  // Save settings before shutdown (or other times as needed)

  leafPrefs.begin("varioPrefs", RW_MODE);

  // save flag to indicate we have previously initialized NVS storage and have saved settings
  // available
  leafPrefs.putBool("nvsInitVario", true);

  // Vario Settings
  leafPrefs.putChar("SINK_ALARM", vario_sinkAlarm);
  leafPrefs.putChar("vario_sensitivity", vario_sensitivity);
  leafPrefs.putChar("CLIMB_AVERAGE", vario_climbAvg);
  leafPrefs.putChar("CLIMB_START", vario_climbStart);
  leafPrefs.putChar("VOLUME_VARIO", vario_volume);
  leafPrefs.putBool("QUIET_MODE", vario_quietMode);
  leafPrefs.putBool("VARIO_TONES", vario_tones);
  leafPrefs.putChar("LIFTY_AIR", vario_liftyAir);
  leafPrefs.putFloat("ALT_SETTING", vario_altSetting);
  leafPrefs.putBool("ALT_SYNC_GPS", vario_altSyncToGPS);
  // GPS & Track Log Settings
  leafPrefs.putBool("DISTANCE_FLOWN", distanceFlownType);
  leafPrefs.putChar("GPS_SETTING", gpsMode);
  leafPrefs.putBool("TRACK_SAVE", log_saveTrack);
  leafPrefs.putBool("AUTO_START", log_autoStart);
  leafPrefs.putBool("AUTO_STOP", log_autoStop);
  leafPrefs.putUChar("LOG_FORMAT", log_format);
  // System Settings
  leafPrefs.putShort("TIME_ZONE", system_timeZone);
  leafPrefs.putChar("VOLUME_SYSTEM", system_volume);
  leafPrefs.putBool("ECO_MODE", system_ecoMode);
  leafPrefs.putBool("AUTO_OFF", system_autoOff);
  leafPrefs.putBool("WIFI_ON", system_wifiOn);
  leafPrefs.putBool("BLUETOOTH_ON", system_bluetoothOn);
  leafPrefs.putBool("SHOW_WARNING", system_showWarning);
  // Boot Flags
  leafPrefs.putBool("ENTER_BOOTLOAD", boot_enterBootloader);
  leafPrefs.putBool("BOOT_TO_ON", boot_toOnState);
  // Display Settings
  leafPrefs.putUChar("CONTRAST", disp_contrast);
  leafPrefs.putUChar("NAVPG_ALT_TYP", disp_navPageAltType);
  leafPrefs.putUChar("THMPG_ALT_TYP", disp_thmPageAltType);
  leafPrefs.putUChar("THMPG_ALT2_TYP", disp_thmPageAlt2Type);
  leafPrefs.putUChar("THMPG_USR1", disp_thmPageUser1);
  leafPrefs.putUChar("THMPG_USR2", disp_thmPageUser2);
  leafPrefs.putBool("SHOW_DEBUG", disp_showDebugPage);
  leafPrefs.putBool("SHOW_THRM", disp_showThmPage);
  leafPrefs.putBool("SHOW_THRM_ADV", disp_showThmAdvPage);
  leafPrefs.putBool("SHOW_NAV", disp_showNavPage);
  // Fanet Settings
  leafPrefs.putUInt("FANET_REGION", (uint32_t)fanet_region);
  leafPrefs.putString("FANET_ADDRESS", fanet_address);
  // Unit Values
  leafPrefs.putBool("UNITS_climb", units_climb);
  leafPrefs.putBool("UNITS_alt", units_alt);
  leafPrefs.putBool("UNITS_temp", units_temp);
  leafPrefs.putBool("UNITS_speed", units_speed);
  leafPrefs.putBool("UNITS_heading", units_heading);
  leafPrefs.putBool("UNITS_distance", units_distance);
  leafPrefs.putBool("UNITS_hours", units_hours);

  leafPrefs.end();
}

void Settings::reset() {
  loadDefaults();
  save();
}

// we probably should never have to call this
void Settings::totallyEraseNVS() {
  nvs_flash_erase();  // erase the NVS partition and...
  nvs_flash_init();   // initialize the NVS partition.
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adjust individual settings

// Contrast Adjustment
void Settings::adjustContrast(Button dir) {
  uint16_t* sound = fx_neutral;
  if (dir == Button::RIGHT)
    sound = fx_increase;
  else if (dir == Button::LEFT)
    sound = fx_decrease;
  else if (dir == Button::CENTER) {  // reset to default
    speaker_playSound(fx_confirm);
    disp_contrast = DEF_CONTRAST;
    display_setContrast(disp_contrast);
    return;
  }

  disp_contrast += dir == Button::RIGHT ? 1 : -1;

  if (disp_contrast > CONTRAST_MAX) {
    disp_contrast = CONTRAST_MAX;
    sound = fx_double;
  } else if (disp_contrast < CONTRAST_MIN) {
    disp_contrast = CONTRAST_MIN;
    sound = fx_double;
  }
  display_setContrast(disp_contrast);
  speaker_playSound(sound);
}

void Settings::adjustSinkAlarm(Button dir) {
  uint16_t* sound = fx_neutral;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    if (++vario_sinkAlarm > 0) {
      vario_sinkAlarm =
          SINK_ALARM_MAX;  // if we were at 0 and now are at positive 1, go back to max sink rate
    } else if (vario_sinkAlarm > SINK_ALARM_MIN) {
      sound = fx_cancel;
      vario_sinkAlarm = 0;  // if we were at MIN (say, -2), jump to 0 (off)
    }
  } else {
    sound = fx_decrease;
    if (--vario_sinkAlarm < SINK_ALARM_MAX) {
      sound = fx_cancel;
      vario_sinkAlarm = 0;  // if we were at max, wrap back to 0
    } else if (vario_sinkAlarm > SINK_ALARM_MIN) {
      vario_sinkAlarm = SINK_ALARM_MIN;  // if we were at 0, and dropped to -1, but still greater
                                         // than the min (-2), jump to -2
    }
  }
  speaker_playSound(sound);
  // TODO: really needed? speaker_updateClimbToneParameters();	// call to adjust sinkRateSpread
  // according to new  vario_sinkAlarm value
}

void Settings::adjustVarioAverage(Button dir) {
  uint16_t* sound = fx_neutral;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    if (++vario_sensitivity >= VARIO_SENSE_MAX) {
      vario_sensitivity = VARIO_SENSE_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    if (--vario_sensitivity <= VARIO_SENSE_MIN) {
      vario_sensitivity = VARIO_SENSE_MIN;
      sound = fx_double;
    }
  }
  speaker_playSound(sound);
}

// climb average goes between 0 and CLIMB_AVERAGE_MAX
void Settings::adjustClimbAverage(Button dir) {
  uint16_t* sound = fx_neutral;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    if (++vario_climbAvg >= CLIMB_AVERAGE_MAX) {
      vario_climbAvg = CLIMB_AVERAGE_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    if (--vario_climbAvg <= 0) {
      vario_climbAvg = 0;
      sound = fx_double;
    }
  }
  speaker_playSound(sound);
}

void Settings::adjustClimbStart(Button dir) {
  uint16_t* sound = fx_neutral;
  uint8_t inc_size = 5;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    if ((vario_climbStart += inc_size) >= CLIMB_START_MAX) {
      vario_climbStart = CLIMB_START_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    if ((vario_climbStart -= inc_size) <= 0) {
      vario_climbStart = 0;
      sound = fx_double;
    }
  }
  speaker_playSound(sound);
}

void Settings::adjustLiftyAir(Button dir) {
  uint16_t* sound = fx_neutral;

  // adjust the setting based on button direction
  if (dir == Button::RIGHT) {
    vario_liftyAir += 1;
    sound = fx_increase;
  } else {
    vario_liftyAir += -1;
    sound = fx_decrease;
  }

  // now scrub the result to ensure we're within bounds
  // if we were at 0 and now are at positive 1, go back to max sink setting
  if (vario_liftyAir > 0) {
    vario_liftyAir = LIFTY_AIR_MAX;
    sound = fx_increase;
  } else if (vario_liftyAir == 0) {  // setting to 0 turns the feature off
    sound = fx_cancel;
  } else if (vario_liftyAir < LIFTY_AIR_MAX) {  // wrap from max back to 0
    sound = fx_cancel;
    vario_liftyAir = 0;
  }
  speaker_playSound(sound);
}

void Settings::adjustVolumeVario(Button dir) {
  uint16_t* sound = fx_neutral;

  if (dir == Button::RIGHT) {
    sound = fx_increase;
    vario_volume++;
    if (vario_volume > VOLUME_MAX) {
      vario_volume = VOLUME_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    vario_volume--;
    if (vario_volume <= 0) {
      vario_volume = 0;
      sound = fx_cancel;  // even if vario volume is set to 0, the system volume may still be turned
                          // on, so we have a sound for turning vario off
    }
  }
  speaker_playSound(sound);
}

void Settings::adjustVolumeSystem(Button dir) {
  uint16_t* sound = fx_neutral;
  if (dir == Button::RIGHT) {
    sound = fx_increase;
    system_volume++;
    if (system_volume > VOLUME_MAX) {
      system_volume = VOLUME_MAX;
      sound = fx_double;
    }
  } else {
    sound = fx_decrease;
    system_volume--;
    if (system_volume <= 0) {
      system_volume = 0;
      sound = fx_cancel;  // we have this line of code for completeness, but the speaker will be
                          // turned off for system sounds so you won't hear it
    }
  }
  speaker_playSound(sound);
}

uint8_t timeZoneIncrement =
    60;  // in minutes.  This allows us to change and adjust by 15 minutes for some regions that
         // have half-hour and quarter-hour time zones.
void Settings::adjustTimeZone(Button dir) {
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
    if (system_timeZone >= TIME_ZONE_MAX) {
      speaker_playSound(fx_double);
      system_timeZone = TIME_ZONE_MAX;
    } else {
      system_timeZone += timeZoneIncrement;
      speaker_playSound(fx_neutral);
    }
  else if (dir == Button::LEFT) {
    if (system_timeZone <= TIME_ZONE_MIN) {
      speaker_playSound(fx_double);
      system_timeZone = TIME_ZONE_MIN;
    } else {
      system_timeZone -= timeZoneIncrement;
      speaker_playSound(fx_neutral);
    }
  }
}

// Change which altitude is shown on the Nav page (Baro Alt, GPS Alt, or Above-Waypoint Alt)
void Settings::adjustDisplayField_navPage_alt(Button dir) {
  if (dir == Button::RIGHT) {
    disp_navPageAltType++;
    if (disp_navPageAltType >= 3) disp_navPageAltType = 0;
  } else {
    if (disp_navPageAltType == 0)
      disp_navPageAltType = 1;
    else
      disp_navPageAltType--;
  }
  speaker_playSound(fx_neutral);
}

// Change which altitude is shown on the Thermal page (Baro Alt or GPS Alt)
void Settings::adjustDisplayField_thermalPage_alt(Button dir) {
  if (dir == Button::RIGHT) {
    disp_thmPageAltType++;
    if (disp_thmPageAltType >= 2) disp_thmPageAltType = 0;
  } else {
    if (disp_thmPageAltType == 0)
      disp_thmPageAltType = 1;
    else
      disp_thmPageAltType--;
  }
  speaker_playSound(fx_neutral);
}

// swap unit settings and play a neutral sound
void Settings::toggleBoolNeutral(bool* unitSetting) {
  *unitSetting = !*unitSetting;
  speaker_playSound(fx_neutral);
}

// flip on/off certain settings and play on/off sounds
void Settings::toggleBoolOnOff(bool* switchSetting) {
  *switchSetting = !*switchSetting;
  if (*switchSetting)
    speaker_playSound(fx_enter);  // if we turned it on
  else
    speaker_playSound(fx_cancel);  // if we turned it off
}
