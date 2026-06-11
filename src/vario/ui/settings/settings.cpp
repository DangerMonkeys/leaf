#include "ui/settings/settings.h"

#include "esp_mac.h"

#include <Preferences.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include "instruments/baro.h"
#include "instruments/gps.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"

#define RW_MODE false
#define RO_MODE true

namespace {
  constexpr float SINK_ALARM_OPTIONS[][11] = {
      {0, -1.2, -1.4, -1.6, -1.8, -2.0, -2.5, -3.0, -4.0, -5.0, -6.0},   // m/s
      {0, -240, -280, -320, -360, -400, -500, -600, -800, -1000, -1200}  // fpm
  };
}

Settings settings;

Preferences leafPrefs;

bool Settings::init() {
  vario_sensitivity.onChange([](const int8_t& newValue) {
    size_t nSamples = 3;
    if (newValue == 1) {
      nSamples = 20;
    } else if (newValue == 2) {
      nSamples = 12;
    } else if (newValue == 3) {
      nSamples = 6;
    } else if (newValue == 4) {
      nSamples = 3;
    } else if (newValue == 5) {
      nSamples = 1;
    }
    baro.setFilterSamples(nSamples);
  });

  loadDefaults();  // load defaults regardless, but we'll overwrite
                   // these with saved user settings (if available)

  // Check if settings have been saved before (or not), then save defaults (or grab saved settings)
  leafPrefs.begin("varioPrefs", RO_MODE);  // open (or create if needed) the varioPrefs namespace
                                           // for user settings/preferences
  bool newBootupVario = !leafPrefs.isKey(
      "nvsInitVario");  // check if we've ever initialized the non volatile storage (nvs), or if
                        // this is a new device boot up for the first time
  leafPrefs.end();

  if (newBootupVario) {
    // handle one-time first boot tasks
    macAddress = getMacAddress();  // capture the device MAC address for use as a unique device ID
    productionTest = DEF_PRODUCTIONTEST;  // flag that the production test has yet been run
    save();                               // save defaults to NVS0

    // save flag to indicate we have previously initialized NVS storage and have saved
    // settings available
    leafPrefs.begin("varioPrefs", RW_MODE);
    leafPrefs.putBool("nvsInitVario", true);
    leafPrefs.end();

    boot_firstTime = true;
  } else {
    retrieve();
    boot_firstTime = false;
  }
  return boot_firstTime;
}

String Settings::getMacAddress() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
  return String(macStr);
}

// Reset Leaf user settings and info to defaults
void Settings::reset() {
  loadDefaults();

  // Clear any other user-supplied information
  // Clear WiFi credentials
  wifi_config_t current_conf;
  esp_wifi_get_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf);
  memset(current_conf.sta.ssid, 0, sizeof(current_conf.sta.ssid));
  memset(current_conf.sta.password, 0, sizeof(current_conf.sta.password));
  esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf);

  save();
}

// Not currently used; we probably should never have to call this
void Settings::totallyEraseNVS() {
  nvs_flash_erase();  // erase the NVS partition and...
  nvs_flash_init();   // initialize the NVS partition.
}

// Wipe user settings as well as factory supplied info like Fanet Address and production test flag
void Settings::factoryResetVario() {
  // reset user settings
  reset();

  // clear additional settings/flags that aren't user settings
  productionTest = DEF_PRODUCTIONTEST;  // erase any record of a production test
  macAddress.clear();                   // clear the MAC address string
  fanet_address.clear();                // clear the FANET address string
  save();                               // store these updated values

  // and finally clear the varioPrefs key to ensure Leaf boots as a new device
  leafPrefs.begin("varioPrefs", RW_MODE);
  leafPrefs.remove("nvsInitVario");
  leafPrefs.end();
}

void Settings::loadDefaults() {
  // Vario Settings
  vario_sinkAlarm = DEF_SINK_ALARM;
  vario_sinkAlarm_units = DEF_SINK_ALARM_UNITS;
  vario_sensitivity.loadDefault();
  vario_climbAvg = DEF_CLIMB_AVERAGE;
  vario_climbStart = DEF_CLIMB_START;
  vario_volume = DEF_VOLUME_VARIO;
  speaker.setVolume(Speaker::SoundChannel::Vario, (SpeakerVolume)vario_volume);
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

  // IGC Pilot & Glider Info
  igc_pilotName = "";
  igc_gliderType = "";
  igc_gliderId = "";
  igc_competitionId = "";
  igc_competitionClass = "";

  // System Settings
  system_timeZone = DEF_TIME_ZONE;
  system_volume = DEF_VOLUME_SYSTEM;
  speaker.setVolume(Speaker::SoundChannel::FX, (SpeakerVolume)system_volume);
  system_ecoMode = DEF_ECO_MODE;
  system_autoOff = DEF_AUTO_OFF;
  system_wifiOn = DEF_WIFI_ON;
  system_bluetoothOn = DEF_BLUETOOTH_ON;
  system_showWarning = DEF_SHOW_WARNING;

  // Developer Options
  dev_menu = DEF_DEV_MENU;
  dev_startLogAtBoot = DEF_DEV_START_LOG_AT_BOOT;
  dev_startDisconnected = DEF_DEV_START_DISCONNECTED;
  dev_fanetFwd = DEF_DEV_FANET_FWD;

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
  disp_showSimplePage = DEF_SHOW_SIMPLE;
  disp_showThmPage = DEF_SHOW_THRM;
  disp_showThmAdvPage = DEF_SHOW_THRM_ADV;
  disp_showNavPage = DEF_SHOW_NAV;
  startPage = DEF_STARTPAGE;

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
  vario_sinkAlarm = leafPrefs.getFloat("SINK_ALARM_VAL", DEF_SINK_ALARM);
  vario_sinkAlarm_units = leafPrefs.getBool("SINK_ALARM_UNIT", DEF_SINK_ALARM_UNITS);
  vario_sensitivity.readFrom(leafPrefs);
  vario_climbAvg = leafPrefs.getChar("CLIMB_AVERAGE");
  vario_climbStart = leafPrefs.getChar("CLIMB_START");
  vario_volume = leafPrefs.getChar("VOLUME_VARIO");
  speaker.setVolume(Speaker::SoundChannel::Vario, (SpeakerVolume)vario_volume);
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
  log_autoStop = leafPrefs.getBool("AUTO_STOP");
  log_format = leafPrefs.getUChar("LOG_FORMAT");

  // System Settings
  system_timeZone = leafPrefs.getShort("TIME_ZONE");
  system_volume = leafPrefs.getChar("VOLUME_SYSTEM");
  speaker.setVolume(Speaker::SoundChannel::FX, (SpeakerVolume)system_volume);
  system_ecoMode = leafPrefs.getBool("ECO_MODE");
  system_autoOff = leafPrefs.getChar("AUTO_OFF");
  system_wifiOn = leafPrefs.getBool("WIFI_ON");
  system_bluetoothOn = leafPrefs.getBool("BLUETOOTH_ON");
  system_showWarning = leafPrefs.getBool("SHOW_WARNING");
  macAddress = leafPrefs.getString("MAC_ADDRESS", getMacAddress());
  productionTest = leafPrefs.getBool("PRODUCTION_TEST", DEF_PRODUCTIONTEST);

  // Developer Options
  dev_menu = leafPrefs.getBool("DEVELOPER_MENU");
  dev_startLogAtBoot = leafPrefs.getBool("DEV_STARTLOG");
  dev_startDisconnected = leafPrefs.getBool("DEV_STARTDISCON");
  dev_fanetFwd = leafPrefs.getBool("DEV_FANET_FWD", DEF_DEV_FANET_FWD);

  // Boot Flags
  boot_enterBootloader = leafPrefs.getBool("ENTER_BOOTLOAD");
  boot_toOnState = leafPrefs.getBool("BOOT_TO_ON");
  boot_firstTime = leafPrefs.getBool("FIRST_BOOT", false);

  // Display Settings
  disp_contrast = leafPrefs.getUChar("CONTRAST");
  if (disp_contrast < CONTRAST_MIN || disp_contrast > CONTRAST_MAX) disp_contrast = DEF_CONTRAST;
  disp_navPageAltType = leafPrefs.getUChar("NAVPG_ALT_TYP");
  disp_thmPageAltType = leafPrefs.getUChar("THMPG_ALT_TYP");
  disp_thmPageAlt2Type = leafPrefs.getUChar("THMPG_ALT2_TYP");
  disp_thmPageUser1 = leafPrefs.getUChar("THMPG_USR1");
  disp_thmPageUser2 = leafPrefs.getUChar("THMPG_USR2");
  disp_showDebugPage = leafPrefs.getBool("SHOW_DEBUG");
  disp_showSimplePage = leafPrefs.getBool("SHOW_SIMPLE", DEF_SHOW_SIMPLE);
  disp_showThmPage = leafPrefs.getBool("SHOW_THRM");
  disp_showThmAdvPage = leafPrefs.getBool("SHOW_THRM_ADV");
  disp_showNavPage = leafPrefs.getBool("SHOW_NAV");
  startPage = leafPrefs.getUChar("START_PAGE", DEF_STARTPAGE);
  if (startPage > (uint8_t)MainPage::Nav) startPage = DEF_STARTPAGE;

  // Fanet settings
  fanet_region = (FanetRadioRegion)leafPrefs.getUInt("FANET_REGION");
  fanet_address = leafPrefs.getString("FANET_ADDRESS");

  // IGC Pilot & Glider Info
  igc_pilotName        = leafPrefs.getString("IGC_PILOT", "");
  igc_gliderType       = leafPrefs.getString("IGC_GLIDER_TYPE", "");
  igc_gliderId         = leafPrefs.getString("IGC_GLIDER_ID", "");
  igc_competitionId    = leafPrefs.getString("IGC_COMP_ID", "");
  igc_competitionClass = leafPrefs.getString("IGC_COMP_CLASS", "");

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

  // Vario Settings
  leafPrefs.putFloat("SINK_ALARM_VAL", vario_sinkAlarm);
  leafPrefs.putBool("SINK_ALARM_UNIT", vario_sinkAlarm_units);
  vario_sensitivity.putInto(leafPrefs);
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
  leafPrefs.putChar("AUTO_OFF", system_autoOff);
  leafPrefs.putBool("WIFI_ON", system_wifiOn);
  leafPrefs.putBool("BLUETOOTH_ON", system_bluetoothOn);
  leafPrefs.putBool("SHOW_WARNING", system_showWarning);
  leafPrefs.putBool("PRODUCTION_TEST", productionTest);
  leafPrefs.putString("MAC_ADDRESS", macAddress);
  // Developer Options
  leafPrefs.putBool("DEVELOPER_MENU", dev_menu);
  leafPrefs.putBool("DEV_STARTLOG", dev_startLogAtBoot);
  leafPrefs.putBool("DEV_STARTDISCON", dev_startDisconnected);
  leafPrefs.putBool("DEV_FANET_FWD", dev_fanetFwd);
  // Boot Flags
  leafPrefs.putBool("ENTER_BOOTLOAD", boot_enterBootloader);
  leafPrefs.putBool("BOOT_TO_ON", boot_toOnState);
  leafPrefs.putBool("FIRST_BOOT", boot_firstTime);
  // Display Settings
  leafPrefs.putUChar("CONTRAST", disp_contrast);
  leafPrefs.putUChar("NAVPG_ALT_TYP", disp_navPageAltType);
  leafPrefs.putUChar("THMPG_ALT_TYP", disp_thmPageAltType);
  leafPrefs.putUChar("THMPG_ALT2_TYP", disp_thmPageAlt2Type);
  leafPrefs.putUChar("THMPG_USR1", disp_thmPageUser1);
  leafPrefs.putUChar("THMPG_USR2", disp_thmPageUser2);
  leafPrefs.putBool("SHOW_DEBUG", disp_showDebugPage);
  leafPrefs.putBool("SHOW_SIMPLE", disp_showSimplePage);
  leafPrefs.putBool("SHOW_THRM", disp_showThmPage);
  leafPrefs.putBool("SHOW_THRM_ADV", disp_showThmAdvPage);
  leafPrefs.putBool("SHOW_NAV", disp_showNavPage);
  leafPrefs.putUChar("START_PAGE", startPage);
  // Fanet Settings
  leafPrefs.putUInt("FANET_REGION", (uint32_t)fanet_region);
  leafPrefs.putString("FANET_ADDRESS", fanet_address);
  // IGC Pilot & Glider Info
  leafPrefs.putString("IGC_PILOT",       igc_pilotName);
  leafPrefs.putString("IGC_GLIDER_TYPE", igc_gliderType);
  leafPrefs.putString("IGC_GLIDER_ID",   igc_gliderId);
  leafPrefs.putString("IGC_COMP_ID",     igc_competitionId);
  leafPrefs.putString("IGC_COMP_CLASS",  igc_competitionClass);
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adjust individual settings

// Contrast Adjustment
void Settings::adjustContrast(Button dir) {
  sound_t sound = fx::neutral;
  if (dir == Button::RIGHT)
    sound = fx::increase;
  else if (dir == Button::LEFT)
    sound = fx::decrease;
  else if (dir == Button::CENTER) {  // reset to default
    speaker.playSound(fx::confirm);
    disp_contrast = DEF_CONTRAST;
    display.setContrast(disp_contrast);
    return;
  }

  disp_contrast += dir == Button::RIGHT ? 1 : -1;

  if (disp_contrast > CONTRAST_MAX) {
    disp_contrast = CONTRAST_MAX;
    sound = fx::doubleClick;
  } else if (disp_contrast < CONTRAST_MIN) {
    disp_contrast = CONTRAST_MIN;
    sound = fx::doubleClick;
  }
  display.setContrast(disp_contrast);
  speaker.playSound(sound);
}

void Settings::adjustSinkAlarm(Button dir) {
  uint8_t opt = vario_sinkAlarm_units ? 1 : 0;  // determine m/s or fpm options
  sound_t sound = fx::neutral;

  // get the size of the sinkAlarm options list
  size_t n = sizeof(SINK_ALARM_OPTIONS[opt]) /
             sizeof(SINK_ALARM_OPTIONS[opt][0]);  // get size of options list

  // then find index of the best-matching setting in the valid options array
  uint8_t index = 0;
  float min_err = 1e9f;
  for (uint8_t i = 0; i < n; i++) {
    float err = abs(vario_sinkAlarm - SINK_ALARM_OPTIONS[opt][i]);
    if (err < min_err) {
      index = i;
      min_err = err;
    }
  }

  // then increase or decrease index based on button direction
  if (dir == Button::RIGHT) {
    sound = fx::increase;
    if (++index >= n) {
      index = 0;
      sound = fx::cancel;
    }
  } else {
    sound = fx::decrease;
    if (index == 0) {
      index = n - 1;
    } else if (--index == 0) {
      sound = fx::cancel;
    }
  }

  // now set the new sink alarm value
  vario_sinkAlarm = SINK_ALARM_OPTIONS[opt][index];

  speaker.playSound(sound);
  // TODO: really needed? speaker_updateClimbToneParameters();	// call to adjust sinkRateSpread
  // according to new  vario_sinkAlarm value
}

void Settings::adjustSinkAlarmUnits(bool units) {
  if (units == vario_sinkAlarm_units)
    return;  // no change
  else {
    uint8_t opt = vario_sinkAlarm_units ? 1 : 0;  // determine m/s or fpm options

    // get the size of the sinkAlarm options list
    size_t n = sizeof(SINK_ALARM_OPTIONS[opt]) /
               sizeof(SINK_ALARM_OPTIONS[opt][0]);  // get size of options list

    // then find index of the best-matching setting in the valid options array
    uint8_t index = 0;
    float min_err = 1e9f;
    for (uint8_t i = 0; i < n; i++) {
      float err = abs(vario_sinkAlarm - SINK_ALARM_OPTIONS[opt][i]);
      if (err < min_err) {
        index = i;
        min_err = err;
      }
    }
    // then switch units
    if (units) {  // switching to fpm
      vario_sinkAlarm = SINK_ALARM_OPTIONS[1][index];
      vario_sinkAlarm_units = true;
    } else {  // switching to m/s
      vario_sinkAlarm = SINK_ALARM_OPTIONS[0][index];
      vario_sinkAlarm_units = false;
    }
  }
}

void Settings::adjustVarioAverage(Button dir) {
  sound_t sound = fx::neutral;

  if (dir == Button::RIGHT) {
    sound = fx::increase;
    if (vario_sensitivity == ++vario_sensitivity) {
      sound = fx::doubleClick;
    }
  } else {
    sound = fx::decrease;
    if (vario_sensitivity == --vario_sensitivity) {
      sound = fx::doubleClick;
    }
  }
  speaker.playSound(sound);
}

// climb average goes between 0 and CLIMB_AVERAGE_MAX
void Settings::adjustClimbAverage(Button dir) {
  sound_t sound = fx::neutral;

  if (dir == Button::RIGHT) {
    sound = fx::increase;
    if (++vario_climbAvg >= CLIMB_AVERAGE_MAX) {
      vario_climbAvg = CLIMB_AVERAGE_MAX;
      sound = fx::doubleClick;
    }
  } else {
    sound = fx::decrease;
    if (--vario_climbAvg <= 0) {
      vario_climbAvg = 0;
      sound = fx::doubleClick;
    }
  }
  speaker.playSound(sound);
}

void Settings::adjustClimbStart(Button dir) {
  sound_t sound = fx::neutral;
  uint8_t inc_size = 5;

  if (dir == Button::RIGHT) {
    sound = fx::increase;
    if ((vario_climbStart += inc_size) >= CLIMB_START_MAX) {
      vario_climbStart = CLIMB_START_MAX;
      sound = fx::doubleClick;
    }
  } else {
    sound = fx::decrease;
    if ((vario_climbStart -= inc_size) <= 0) {
      vario_climbStart = 0;
      sound = fx::doubleClick;
    }
  }
  speaker.playSound(sound);
}

void Settings::adjustLiftyAir(Button dir) {
  sound_t sound = fx::neutral;

  // adjust the setting based on button direction
  if (dir == Button::RIGHT) {
    vario_liftyAir += 1;
    sound = fx::increase;
  } else {
    vario_liftyAir += -1;
    sound = fx::decrease;
  }

  // now scrub the result to ensure we're within bounds
  // if we were at 0 and now are at positive 1, go back to max sink setting
  if (vario_liftyAir > 0) {
    vario_liftyAir = LIFTY_AIR_MAX;
    sound = fx::increase;
  } else if (vario_liftyAir == 0) {  // setting to 0 turns the feature off
    sound = fx::cancel;
  } else if (vario_liftyAir < LIFTY_AIR_MAX) {  // wrap from max back to 0
    sound = fx::cancel;
    vario_liftyAir = 0;
  }
  speaker.playSound(sound);
}

void Settings::adjustVolumeVario(Button dir) {
  sound_t sound = fx::neutral;

  if (dir == Button::RIGHT) {
    sound = fx::increase;
    vario_volume++;
    if (vario_volume > VOLUME_MAX) {
      vario_volume = VOLUME_MAX;
      sound = fx::doubleClick;
    } else {
      speaker.setVolume(Speaker::SoundChannel::Vario, (SpeakerVolume)vario_volume);
    }
  } else {
    sound = fx::decrease;
    vario_volume--;
    if (vario_volume <= 0) {
      vario_volume = 0;
      sound = fx::cancel;  // even if vario volume is set to 0, the system volume may still be
                           // turned on, so we have a sound for turning vario off
    }
    speaker.setVolume(Speaker::SoundChannel::Vario, (SpeakerVolume)vario_volume);
  }
  speaker.playSound(sound);
}

void Settings::adjustVolumeSystem(Button dir) {
  sound_t sound = fx::neutral;
  if (dir == Button::RIGHT) {
    sound = fx::increase;
    system_volume++;
    if (system_volume > VOLUME_MAX) {
      system_volume = VOLUME_MAX;
      sound = fx::doubleClick;
    } else {
      speaker.setVolume(Speaker::SoundChannel::FX, (SpeakerVolume)system_volume);
    }
  } else {
    sound = fx::decrease;
    system_volume--;
    if (system_volume <= 0) {
      system_volume = 0;
      sound = fx::cancel;  // we have this line of code for completeness, but the speaker will be
                           // turned off for system sounds so you won't hear it
    }
    speaker.setVolume(Speaker::SoundChannel::FX, (SpeakerVolume)system_volume);
  }
  speaker.playSound(sound);
}

uint8_t timeZoneIncrement =
    60;  // in minutes.  This allows us to change and adjust by 15 minutes for some regions that
         // have half-hour and quarter-hour time zones.
void Settings::adjustTimeZone(Button dir) {
  if (dir == Button::CENTER) {  // switch from half-hour to full-hour increments
    if (timeZoneIncrement == 60) {
      timeZoneIncrement = 15;
      speaker.playSound(fx::increase);
    } else if (timeZoneIncrement == 15) {
      timeZoneIncrement = 60;
      speaker.playSound(fx::decrease);
    }
  }
  if (dir == Button::RIGHT)
    if (system_timeZone >= TIME_ZONE_MAX) {
      speaker.playSound(fx::doubleClick);
      system_timeZone = TIME_ZONE_MAX;
    } else {
      system_timeZone += timeZoneIncrement;
      speaker.playSound(fx::neutral);
    }
  else if (dir == Button::LEFT) {
    if (system_timeZone <= TIME_ZONE_MIN) {
      speaker.playSound(fx::doubleClick);
      system_timeZone = TIME_ZONE_MIN;
    } else {
      system_timeZone -= timeZoneIncrement;
      speaker.playSound(fx::neutral);
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
  speaker.playSound(fx::neutral);
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
  speaker.playSound(fx::neutral);
}

// swap unit settings and play a neutral sound
void Settings::toggleBoolNeutral(bool* unitSetting) {
  *unitSetting = !*unitSetting;
  speaker.playSound(fx::neutral);
}

// flip on/off certain settings and play on/off sounds
void Settings::toggleBoolOnOff(bool* switchSetting) {
  *switchSetting = !*switchSetting;
  if (*switchSetting)
    speaker.playSound(fx::enter);  // if we turned it on
  else
    speaker.playSound(fx::cancel);  // if we turned it off
}

String Settings::toJson() const {
  JsonDocument doc;

  // Vario
  doc["vario_sinkAlarm"]      = vario_sinkAlarm;
  doc["vario_sinkAlarm_units"]= vario_sinkAlarm_units;
  doc["vario_sensitivity"]    = (int8_t)vario_sensitivity;
  doc["vario_climbAvg"]       = vario_climbAvg;
  doc["vario_climbStart"]     = vario_climbStart;
  doc["vario_volume"]         = vario_volume;
  doc["vario_quietMode"]      = vario_quietMode;
  doc["vario_tones"]          = vario_tones;
  doc["vario_liftyAir"]       = vario_liftyAir;
  doc["vario_altSetting"]     = vario_altSetting;
  doc["vario_altSyncToGPS"]   = vario_altSyncToGPS;

  // GPS & Track Log
  doc["distanceFlownType"]    = distanceFlownType;
  doc["gpsMode"]              = gpsMode;
  doc["log_saveTrack"]        = log_saveTrack;
  doc["log_autoStart"]        = log_autoStart;
  doc["log_autoStop"]         = log_autoStop;
  doc["log_format"]           = log_format;

  // IGC Pilot & Glider Info
  doc["igc_pilotName"]        = igc_pilotName.c_str();
  doc["igc_gliderType"]       = igc_gliderType.c_str();
  doc["igc_gliderId"]         = igc_gliderId.c_str();
  doc["igc_competitionId"]    = igc_competitionId.c_str();
  doc["igc_competitionClass"] = igc_competitionClass.c_str();

  // System
  doc["system_timeZone"]      = system_timeZone;
  doc["system_volume"]        = system_volume;
  doc["system_ecoMode"]       = system_ecoMode;
  doc["system_autoOff"]       = system_autoOff;
  doc["system_wifiOn"]        = system_wifiOn;
  doc["system_bluetoothOn"]   = system_bluetoothOn;
  doc["system_showWarning"]   = system_showWarning;

  // Display
  doc["disp_contrast"]        = disp_contrast;
  doc["disp_navPageAltType"]  = disp_navPageAltType;
  doc["disp_thmPageAltType"]  = disp_thmPageAltType;
  doc["disp_thmPageAlt2Type"] = disp_thmPageAlt2Type;
  doc["disp_thmPageUser1"]    = disp_thmPageUser1;
  doc["disp_thmPageUser2"]    = disp_thmPageUser2;
  doc["disp_showDebugPage"]   = disp_showDebugPage;
  doc["disp_showSimplePage"]  = disp_showSimplePage;
  doc["disp_showThmPage"]     = disp_showThmPage;
  doc["disp_showThmAdvPage"]  = disp_showThmAdvPage;
  doc["disp_showNavPage"]     = disp_showNavPage;
  doc["startPage"]            = startPage;

  // FANET
  doc["fanet_region"]         = (uint32_t)fanet_region;
  doc["fanet_address"]        = fanet_address.c_str();

  // Units
  doc["units_climb"]          = units_climb;
  doc["units_alt"]            = units_alt;
  doc["units_temp"]           = units_temp;
  doc["units_speed"]          = units_speed;
  doc["units_heading"]        = units_heading;
  doc["units_distance"]       = units_distance;
  doc["units_hours"]          = units_hours;

  // Read-only device info
  doc["macAddress"]           = macAddress.c_str();

  String out;
  serializeJson(doc, out);
  return out;
}

bool Settings::applyFromJson(const JsonVariantConst& v) {
  // Reject anything that isn't a JSON object
  if (!v.is<JsonObjectConst>()) return false;
  JsonObjectConst doc = v.as<JsonObjectConst>();

  // Vario
  if (doc["vario_sinkAlarm"].is<float>())
    vario_sinkAlarm = doc["vario_sinkAlarm"].as<float>();
  if (doc["vario_sinkAlarm_units"].is<bool>())
    vario_sinkAlarm_units = doc["vario_sinkAlarm_units"].as<bool>();
  if (doc["vario_sensitivity"].is<int>()) {
    int8_t v_sens = doc["vario_sensitivity"].as<int8_t>();
    vario_sensitivity = v_sens;  // CharSetting clamps automatically
  }
  if (doc["vario_climbAvg"].is<int>())
    vario_climbAvg = constrain((int)doc["vario_climbAvg"].as<int>(), 0, CLIMB_AVERAGE_MAX);
  if (doc["vario_climbStart"].is<int>())
    vario_climbStart = constrain((int)doc["vario_climbStart"].as<int>(), 0, CLIMB_START_MAX);
  if (doc["vario_volume"].is<int>()) {
    vario_volume = constrain((int)doc["vario_volume"].as<int>(), 0, VOLUME_MAX);
    speaker.setVolume(Speaker::SoundChannel::Vario, (SpeakerVolume)vario_volume);
  }
  if (doc["vario_quietMode"].is<bool>())
    vario_quietMode = doc["vario_quietMode"].as<bool>();
  if (doc["vario_tones"].is<bool>())
    vario_tones = doc["vario_tones"].as<bool>();
  if (doc["vario_liftyAir"].is<int>())
    vario_liftyAir = constrain((int)doc["vario_liftyAir"].as<int>(), LIFTY_AIR_MAX, 0);
  if (doc["vario_altSetting"].is<float>())
    vario_altSetting = doc["vario_altSetting"].as<float>();
  if (doc["vario_altSyncToGPS"].is<bool>())
    vario_altSyncToGPS = doc["vario_altSyncToGPS"].as<bool>();

  // GPS & Track Log
  if (doc["distanceFlownType"].is<bool>())
    distanceFlownType = doc["distanceFlownType"].as<bool>();
  if (doc["gpsMode"].is<int>())
    gpsMode = doc["gpsMode"].as<int8_t>();
  if (doc["log_saveTrack"].is<bool>())
    log_saveTrack = doc["log_saveTrack"].as<bool>();
  if (doc["log_autoStart"].is<bool>())
    log_autoStart = doc["log_autoStart"].as<bool>();
  if (doc["log_autoStop"].is<bool>())
    log_autoStop = doc["log_autoStop"].as<bool>();
  if (doc["log_format"].is<int>())
    log_format = constrain((int)doc["log_format"].as<int>(), 0, SETTING_LOG_FORMAT_ENTRIES - 1);

  // IGC Pilot & Glider Info
  if (doc["igc_pilotName"].is<const char*>())
    igc_pilotName = doc["igc_pilotName"].as<const char*>();
  if (doc["igc_gliderType"].is<const char*>())
    igc_gliderType = doc["igc_gliderType"].as<const char*>();
  if (doc["igc_gliderId"].is<const char*>())
    igc_gliderId = doc["igc_gliderId"].as<const char*>();
  if (doc["igc_competitionId"].is<const char*>())
    igc_competitionId = doc["igc_competitionId"].as<const char*>();
  if (doc["igc_competitionClass"].is<const char*>())
    igc_competitionClass = doc["igc_competitionClass"].as<const char*>();

  // System
  if (doc["system_timeZone"].is<int>())
    system_timeZone = constrain((int)doc["system_timeZone"].as<int>(), TIME_ZONE_MIN, TIME_ZONE_MAX);
  if (doc["system_volume"].is<int>()) {
    system_volume = constrain((int)doc["system_volume"].as<int>(), 0, VOLUME_MAX);
    speaker.setVolume(Speaker::SoundChannel::FX, (SpeakerVolume)system_volume);
  }
  if (doc["system_ecoMode"].is<bool>())
    system_ecoMode = doc["system_ecoMode"].as<bool>();
  if (doc["system_autoOff"].is<int>())
    system_autoOff = constrain((int)doc["system_autoOff"].as<int>(), 0, AUTO_OFF_MAX);
  if (doc["system_wifiOn"].is<bool>())
    system_wifiOn = doc["system_wifiOn"].as<bool>();
  if (doc["system_bluetoothOn"].is<bool>())
    system_bluetoothOn = doc["system_bluetoothOn"].as<bool>();
  if (doc["system_showWarning"].is<bool>())
    system_showWarning = doc["system_showWarning"].as<bool>();

  // Display
  if (doc["disp_contrast"].is<int>())
    disp_contrast = constrain((int)doc["disp_contrast"].as<int>(), CONTRAST_MIN, CONTRAST_MAX);
  if (doc["disp_navPageAltType"].is<int>())
    disp_navPageAltType = doc["disp_navPageAltType"].as<uint8_t>();
  if (doc["disp_thmPageAltType"].is<int>())
    disp_thmPageAltType = doc["disp_thmPageAltType"].as<uint8_t>();
  if (doc["disp_thmPageAlt2Type"].is<int>())
    disp_thmPageAlt2Type = doc["disp_thmPageAlt2Type"].as<uint8_t>();
  if (doc["disp_thmPageUser1"].is<int>())
    disp_thmPageUser1 = doc["disp_thmPageUser1"].as<uint8_t>();
  if (doc["disp_thmPageUser2"].is<int>())
    disp_thmPageUser2 = doc["disp_thmPageUser2"].as<uint8_t>();
  if (doc["disp_showDebugPage"].is<bool>())
    disp_showDebugPage = doc["disp_showDebugPage"].as<bool>();
  if (doc["disp_showSimplePage"].is<bool>())
    disp_showSimplePage = doc["disp_showSimplePage"].as<bool>();
  if (doc["disp_showThmPage"].is<bool>())
    disp_showThmPage = doc["disp_showThmPage"].as<bool>();
  if (doc["disp_showThmAdvPage"].is<bool>())
    disp_showThmAdvPage = doc["disp_showThmAdvPage"].as<bool>();
  if (doc["disp_showNavPage"].is<bool>())
    disp_showNavPage = doc["disp_showNavPage"].as<bool>();
  if (doc["startPage"].is<int>())
    startPage = doc["startPage"].as<uint8_t>();

  // FANET
  if (doc["fanet_region"].is<int>())
    fanet_region = (FanetRadioRegion)doc["fanet_region"].as<uint32_t>();
  if (doc["fanet_address"].is<const char*>())
    fanet_address = doc["fanet_address"].as<const char*>();

  // Units
  if (doc["units_climb"].is<bool>())    units_climb    = doc["units_climb"].as<bool>();
  if (doc["units_alt"].is<bool>())      units_alt      = doc["units_alt"].as<bool>();
  if (doc["units_temp"].is<bool>())     units_temp     = doc["units_temp"].as<bool>();
  if (doc["units_speed"].is<bool>())    units_speed    = doc["units_speed"].as<bool>();
  if (doc["units_heading"].is<bool>())  units_heading  = doc["units_heading"].as<bool>();
  if (doc["units_distance"].is<bool>()) units_distance = doc["units_distance"].as<bool>();
  if (doc["units_hours"].is<bool>())    units_hours    = doc["units_hours"].as<bool>();

  // macAddress is read-only; silently ignore if present in the JSON

  return true;
}

void Settings::adjustAutoOff(Button dir) {
  uint8_t autoOffOptions[8] = {0, 1, 5, 10, 15, 30, 45, 60};  // in minutes, where 0 = DISABLE
  for (uint8_t i = 0; i < sizeof(autoOffOptions) / sizeof(autoOffOptions[0]); i++) {
    if (system_autoOff <= autoOffOptions[i]) {
      // found the current setting in the options list, now adjust based on button press
      if (dir == Button::RIGHT || dir == Button::CENTER) {
        if (i >= sizeof(autoOffOptions) / sizeof(autoOffOptions[0]) - 1) {
          speaker.playSound(fx::doubleClick);
          system_autoOff = autoOffOptions[sizeof(autoOffOptions) / sizeof(autoOffOptions[0]) - 1];
        } else {
          system_autoOff = autoOffOptions[i + 1];
          speaker.playSound(fx::neutral);
        }
      } else if (dir == Button::LEFT) {
        if (i > 0) {
          system_autoOff = autoOffOptions[i - 1];
          if (system_autoOff != 0) {
            speaker.playSound(fx::neutral);
          } else {
            speaker.playSound(fx::cancel);
          }
        } else {
          speaker.playSound(fx::cancel);
          system_autoOff = autoOffOptions[0];
        }
      }
      break;
    }
    if (i == sizeof(autoOffOptions) / sizeof(autoOffOptions[0]) - 1) {
      // if we don't find the current setting in the options list, then set it to default
      system_autoOff = autoOffOptions[0];
      speaker.playSound(fx::cancel);
      break;
    }
  }
}
