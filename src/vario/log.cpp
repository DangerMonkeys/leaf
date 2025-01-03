
#include "log.h"

#include <Arduino.h>

#include "IMU.h"
#include "SDcard.h"
#include "baro.h"
#include "gps.h"
#include "settings.h"
#include "speaker.h"
#include "tempRH.h"

bool DATAFILE = true;  // set to false to disable data logging to SDcard

// Flight Timer
uint32_t flightTimerSec = 0;
bool flightTimerRunning = 0;
bool flightTimerResetting = 0;
char flightTimerString[] = "0:00:00";  // H:MM:SS -> 7 characters max for string.  If it hits 10
                                       // hours, will re-format to _HH:MM_
char flightTimerStringShort[] =
    "00:00";  // MM:SS -> 5 characters max for string.  If it hits 1 hour, will change to HH:MM
char flightTimerStringLong[] = "00:00:00";  // HH:MM:SS -> 8 characters.

// values for logbook entries
LOGBOOK logbook;
/*
bool logFlightTrackStarted = false;

int32_t log_alt = 0;
int32_t log_alt_start = 0;
int32_t log_alt_end = 0;
int32_t log_alt_max = 0;
int32_t log_alt_min = 0;
int32_t log_alt_above_launch = 0;
int32_t log_alt_above_launch_max = 0;

int32_t log_climb = 0;
int32_t log_climb_max = 0;
int32_t log_climb_min = 0;

float log_temp = 0;
float log_temp_max = 0;
float log_temp_min = 0;

int32_t log_speed = 0;
int32_t log_speed_max = 0;
int32_t log_speed_min = 0;
*/

void log_init() {}

//////////////////////////////////////////////////////////////////////////////////////////
// UPDATE - Main function to run every second
void log_update() {
  uint32_t time = micros();

  // everything log-related if the timer IS RUNNING
  if (flightTimerRunning) {
    flightTimerSec += 1;

    // start the Track if needed (we check every update, in case we didn't have a GPS fix.  This way
    // we can start track log writing as soon as we DO get a fix)
    if (!logbook.flightTrackStarted) {
      if (gps.location.isValid()) {
        if (ALT_SYNC_GPS)
          settings_matchGPSAlt();  // sync pressure alt to GPS alt when log starts if the auto-sync
                                   // setting is turned on
        logbook.flightTrackStarted = SDcard_createTrackFile(
            log_createFileName());  // flag that we've started a log if we actually have
                                    // successfully started a log file on SDCard.  If this returns
                                    // false, we can keep trying to save a log file until it's
                                    // successful.
      }
    }

    // start the data file logging if needed
    if (DATAFILE && !logbook.dataFileStarted) {
      logbook.dataFileStarted = SDcard_createDataFile(log_createTestDataFileName());
    }

    uint32_t logSave_time = micros();

    // save datapoints to the track
    if (logbook.flightTrackStarted) {
      SDcard_writeLogData(log_getKMLCoordinates());
    }

    logSave_time = micros() - logSave_time;
    Serial.print("log save: ");
    Serial.println(logSave_time);

    // capture any records for this flight
    log_captureValues();
    log_checkMinMaxValues();

    uint32_t timer_time = micros();

    // finally, check if we should auto-stop the timer because we've been sitting idle for long
    // enough
    if (AUTO_STOP && flightTimer_autoStop()) {
      flightTimer_stop();
    }

    timer_time = micros() - timer_time;
    Serial.print("timerCheck: ");
    Serial.println(timer_time);

  } else {  // if timer NOT running, check if we should auto-start it
    if (AUTO_START && flightTimer_autoStart()) {
      flightTimer_start();
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Auto Start & Stop check functions.

uint8_t autoStartCounter = 0;
uint8_t autoStopCounter = 0;
int32_t autoStopAltitude = 0;

bool flightTimer_autoStart() {
  bool startTheTimer = false;  // default to not auto-start

  // we will auto-start if EITHER the GPS speed or the Altitude change triggers the starting
  // thresholds.

  // keep track of how many times (seconds) we've been continuously over the min speed threshold
  if (gps.speed.mph() > AUTO_START_MIN_SPEED) {
    autoStartCounter++;
    if (autoStartCounter >= AUTO_START_MIN_SEC) {
      startTheTimer = true;
      Serial.println("****************************** autoStart TRUE via speed");
    }
  } else {
    autoStartCounter = 0;
  }

  // check if current altitude has changed enough from startup to trigger timer start
  int32_t altDifference = baro.alt - baro.altInitial;
  if (altDifference < 0) altDifference *= -1;
  if (altDifference > AUTO_START_MIN_ALT) {
    startTheTimer = true;
    Serial.println("****************************** autoStart TRUE via alt");
  }

  Serial.print("S T A R T   Counter: ");
  Serial.print(autoStartCounter);
  Serial.print("   Alt Diff: ");
  Serial.print(altDifference);
  Serial.print("  StartTheTimer? : ");
  Serial.println(startTheTimer);

  if (startTheTimer) {
    autoStartCounter = 0;
  }

  return startTheTimer;
}

bool flightTimer_autoStop() {
  bool stopTheTimer = false;  // default to not auto-stop

  // we will auto-stop only if BOTH the GPS speed AND the Altitude change trigger the stopping
  // thresholds.

  // First check if altitude is stable
  int32_t altDifference = baro.alt - autoStopAltitude;
  if (altDifference < 0) altDifference *= -1;
  if (altDifference < AUTO_STOP_MAX_ALT) {
    // then check if GPS speed is slow enough
    if (gps.speed.mph() < AUTO_STOP_MAX_SPEED) {
      autoStopCounter++;
      if (autoStopCounter >= AUTO_STOP_MIN_SEC) {
        stopTheTimer = true;
      }
    } else {
      autoStopCounter = 0;
    }

  } else {
    autoStopAltitude =
        baro.alt;  // reset the comparison altitude to present altitude, since it's still changing
  }

  Serial.print(" ** STOP ** Counter: ");
  Serial.print(autoStopCounter);
  Serial.print("   Alt Diff: ");
  Serial.print(altDifference);
  Serial.print("  stopTheTimer? : ");
  Serial.println(stopTheTimer);

  if (stopTheTimer) {
    baro.altInitial = baro.alt;  // reset initial alt (to enable properly checking again for
                                 // auto-start conditions)
  }

  return stopTheTimer;
}

//////////////////////////////////////////////////////////////////////////////////
// FLight Timer Management Functions

// check if running
bool flightTimer_isRunning() { return flightTimerRunning; }

// start timer
void flightTimer_start() {
  if (!flightTimerRunning) {
    // start timer
    speaker_playSound(fx_enter);
    flightTimerRunning = 1;

    // if Altimeter GPS-SYNC is on, reset altimeter setting so baro matches GPS when log is started
    if (ALT_SYNC_GPS) settings_matchGPSAlt();

    // starting values
    baro_resetLaunchAlt();
    logbook.alt_start = baro.altAtLaunch;

    // get first set of log values
    log_captureValues();

    // initial min/max values
    logbook.alt_max = logbook.alt_min = logbook.alt_start;
    logbook.climb_max = logbook.climb_min = logbook.climb;
    logbook.speed_max = logbook.speed_min = logbook.speed;
    logbook.temperature_max = logbook.temperature_min = logbook.temperature;

    // Finally, save current new altitude as auto-stop altitude
    autoStopAltitude = baro.alt;
  }
}

// stop timer
void flightTimer_stop() {
  // play stopping sound
  if (!flightTimerResetting)
    speaker_playSound(fx_cancel);  // only play sound if not in the process of resetting (the
                                   // resetting sound will play from the reset function)

  // finish up log file if timer was running
  if (flightTimerRunning) {
    // ending values
    logbook.alt_end = baro.alt;
    // TODO: save other min/max values in a csv or similar log file.  Also description of KML file
    // maybe?

    // if a KML track log was started
    if (logbook.flightTrackStarted) {
      logbook.flightTrackStarted = false;
      Serial.println("closing Log");
      SDcard_writeLogFooter(log_createTrackFileName(), log_createTrackDescription());
      // TODO: other KML file additions; maybe adding description and min/max and other stuff
    }

    // if a flight data file was started
    if (logbook.dataFileStarted) {
      logbook.dataFileStarted = false;
      SDcard_closeDataFile();
    }
  }
  flightTimerRunning = 0;
}

void flightTimer_toggle() {
  if (flightTimerRunning)
    flightTimer_stop();
  else
    flightTimer_start();
}

void flightTimer_reset() {
  flightTimerResetting = true;
  if (flightTimerRunning) flightTimer_stop();
  speaker_playSound(fx_exit);
  flightTimerSec = 0;
  flightTimerResetting = false;
}

uint32_t flightTimer_getTime() { return flightTimerSec; }

char* flightTimer_getString(bool shortString) {
  flightTimer_updateStrings();
  if (shortString)
    return flightTimerStringShort;
  else
    return flightTimerString;
}

void flightTimer_updateStrings() {
  // char * flightTimerString = "0:00:00";    // H:MM:SS -> 7 characters max for string.  If it hits
  // 10 hours, will re-format to _HH:MM_ char * flightTimerStringShort = "00:00"; // MM:SS -> 5
  // characters max for string.  If it hits 1 hour, will change to HH:MM

  uint8_t hours = flightTimerSec / 3600;
  uint8_t mins = (flightTimerSec / 60) % 60;
  uint8_t secs = flightTimerSec % 60;

  uint8_t position = 0;

  // update flightTimerString
  if (hours > 9) {
    if (hours > 99)
      //      "000:00 "  HHH:MM_
      flightTimerString[position] = '0' + hours / 100;
    else
      //      " 00:00 "  _HH:MM_
      flightTimerString[position] = ' ';
    flightTimerString[++position] = '0' + hours / 10;
    flightTimerString[++position] = '0' + hours % 10;
    flightTimerString[++position] = ':';
    flightTimerString[++position] = '0' + mins / 10;
    flightTimerString[++position] = '0' + mins % 10;
    flightTimerString[++position] = ' ';
  } else if (hours > 0) {
    //      "0:00:00"  H:MM:SS
    flightTimerString[position] = '0' + hours;
    flightTimerString[++position] = ':';
    flightTimerString[++position] = '0' + mins / 10;
    flightTimerString[++position] = '0' + mins % 10;
    flightTimerString[++position] = ':';
    flightTimerString[++position] = '0' + secs / 10;
    flightTimerString[++position] = '0' + secs % 10;
  } else {
    flightTimerString[position++] = ' ';
    if (mins > 9)
      //      " 00:00 "  _MM:SS_
      flightTimerString[position] = '0' + mins / 10;
    else
      //      "  0:00 "  __M:SS_
      flightTimerString[position] = ' ';

    flightTimerString[++position] = '0' + mins % 10;
    flightTimerString[++position] = ':';
    flightTimerString[++position] = '0' + secs / 10;
    flightTimerString[++position] = '0' + secs % 10;
    flightTimerString[++position] = ' ';
  }

  position = 0;

  // update flightTimerStringShort
  // char * flightTimerStringShort = "00:00"; // MM:SS -> 5 characters max for string.  If it hits 1
  // hour, will change to HH:MM
  if (hours > 0) {
    if (hours > 9)
      flightTimerStringShort[position] = '0' + hours / 10;
    else
      flightTimerStringShort[position] = ' ';
    flightTimerStringShort[++position] = '0' + hours % 10;
    flightTimerStringShort[++position] = ':';
    flightTimerStringShort[++position] = '0' + mins / 10;
    flightTimerStringShort[++position] = '0' + mins % 10;
  } else {
    if (mins > 9)
      flightTimerStringShort[position] = '0' + mins / 10;
    else
      flightTimerStringShort[position] = ' ';
    flightTimerStringShort[++position] = '0' + mins % 10;
    flightTimerStringShort[++position] = ':';
    flightTimerStringShort[++position] = '0' + secs / 10;
    flightTimerStringShort[++position] = '0' + secs % 10;
  }

  position = 0;

  // update flightTimerStringLong
  if (hours > 99) {
    //      "000:00 "  HHH:MM__
    flightTimerStringLong[position] = '0' + hours / 100;
  } else {
    //      "00:00:00" HH:MM:SS
    flightTimerStringLong[++position] = '0' + hours / 10;
    flightTimerStringLong[++position] = '0' + hours % 10;
    flightTimerStringLong[++position] = ':';
    flightTimerStringLong[++position] = '0' + mins / 10;
    flightTimerStringLong[++position] = '0' + mins % 10;
    flightTimerStringLong[++position] = ':';
    flightTimerStringLong[++position] = '0' + secs / 10;
    flightTimerStringLong[++position] = '0' + secs % 10;
  }
}

void log_captureValues() {
  logbook.alt = baro.alt;
  logbook.alt_above_launch = baro.altAboveLaunch;
  logbook.climb = baro.climbRateFiltered;
  logbook.speed = gps.speed.kmph();
  logbook.temperature = tempRH_getTemp();
}

void log_checkMinMaxValues() {
  uint32_t time = micros();

  // check altitude values for log records
  if (logbook.alt > logbook.alt_max) {
    logbook.alt_max = logbook.alt;
    if (logbook.alt_above_launch > logbook.alt_above_launch_max)
      logbook.alt_above_launch_max =
          logbook.alt_above_launch;  // we only need to check for max above-launch values if we're
                                     // also setting a new altitude max.
  } else if (logbook.alt < logbook.alt_min) {
    logbook.alt_min = logbook.alt;
  }

  // check climb values for log records
  logbook.climb = baro.climbRateFiltered;
  if (logbook.climb > logbook.climb_max) {
    logbook.climb_max = logbook.climb;
  } else if (logbook.climb < logbook.climb_min) {
    logbook.climb_min = logbook.climb;
  }

  // check temperature values for log records
  logbook.temperature = tempRH_getTemp();
  if (logbook.temperature > logbook.temperature_max) {
    logbook.temperature_max = logbook.temperature;
  } else if (logbook.temperature < logbook.temperature_min) {
    logbook.temperature_min = logbook.temperature;
  }

  time = micros() - time;
  Serial.print("checkMinMax: ");
  Serial.println(time);
}

String log_getKMLCoordinates() {
  uint32_t time = micros();

  String lonPoint = String(gps.location.lng(), 7);
  String latPoint = String(gps.location.lat(), 7);
  String altPoint = String(gps.altitude.meters(), 2);
  String logPointStr = lonPoint + "," + latPoint + "," + altPoint + "\n";

  time = micros() - time;

  Serial.print("Get KML Coords: ");

  Serial.println(time);
  Serial.println(logPointStr);

  return logPointStr;
}

// filename for Flight Track log file (GPS points every second)
String log_createFileName() {
  String fileTitle = "FlightTrack";
  String fileDate = String(gps_getLocalDate());
  int32_t timeInMinutes =
      (gps.time.hour() * 60 + gps.time.minute() + 24 * 60 + TIME_ZONE) % (24 * 60);
  uint8_t timeHours = timeInMinutes / 60;
  uint8_t timeMinutes = timeInMinutes % 60;
  String fileTime = String(timeHours / 10) + String(timeHours % 10) + String(timeMinutes / 10) +
                    String(timeMinutes % 10);
  // TODO: add seconds in case two files are started within a minute

  Serial.println(timeInMinutes);
  Serial.println(timeHours);
  Serial.println(timeMinutes);
  Serial.println(timeMinutes / 10);
  Serial.println(timeMinutes % 10);

  String fileName = fileTitle + "_" + fileDate + "_" + fileTime + ".kml";
  Serial.println(fileName);

  return fileName;
}

// file name for Test Data file (all flight sensor data saved in real time -- for debugging and
// tuning purposes)
String log_createTestDataFileName() {
  String fileTitle = "TestData";
  String fileDate = String(gps_getLocalDate());
  int32_t timeInMinutes =
      (gps.time.hour() * 60 + gps.time.minute() + 24 * 60 + TIME_ZONE) % (24 * 60);
  uint8_t timeHours = timeInMinutes / 60;
  uint8_t timeMinutes = timeInMinutes % 60;
  String fileTime = String(timeHours / 10) + String(timeHours % 10) + String(timeMinutes / 10) +
                    String(timeMinutes % 10);

  String fileName = fileTitle + "_" + fileDate + "_" + fileTime + ".csv";
  Serial.println(fileName);

  return fileName;
}

String log_createTrackFileName() {
  Serial.print("creating track file name...");
  String trackname = "Flight Time: ";
  trackname.concat(flightTimerStringLong);
  Serial.println("finished");
  return trackname;
}

String log_createTrackDescription() {
  Serial.print("creating track description...");

  String alt_units;
  if (UNITS_alt)
    alt_units = "(ft)";
  else
    alt_units = "(m)";

  String climb_units;
  if (UNITS_climb)
    climb_units = "(fpm)";
  else
    climb_units = "(m/s)";

  String temp_units;
  if (UNITS_temp)
    temp_units = "(F)";
  else
    temp_units = "(C)";

  // convert values to proper units before printing/saving
  logbook.alt_start = baro_altToUnits(logbook.alt_start, UNITS_alt);
  logbook.alt_max = baro_altToUnits(logbook.alt_max, UNITS_alt);
  logbook.alt_min = baro_altToUnits(logbook.alt_min, UNITS_alt);
  logbook.alt_end = baro_altToUnits(logbook.alt_end, UNITS_alt);
  logbook.alt_above_launch_max = baro_altToUnits(logbook.alt_above_launch_max, UNITS_alt);

  String stringClimbMax;
  String stringClimbMin;
  if (UNITS_climb) {
    stringClimbMax = String(baro_climbToUnits(logbook.climb_max, UNITS_climb), 0);
    stringClimbMin = String(baro_climbToUnits(logbook.climb_min, UNITS_climb), 0);
  } else {
    stringClimbMax = String(baro_climbToUnits(logbook.climb_max, UNITS_climb), 1);
    stringClimbMin = String(baro_climbToUnits(logbook.climb_min, UNITS_climb), 1);
  }

  String trackdescription =
      "Altitude " + alt_units + " Start: " + logbook.alt_start + " Max: " + logbook.alt_max +
      " Above Launch: " + logbook.alt_above_launch_max + " Min: " + logbook.alt_min +
      " End: " + logbook.alt_end + "\n" + "Climb " + climb_units + " Max: " + stringClimbMax +
      " Min: " + stringClimbMin + "\n" + "Temp " + temp_units + " Max: " + logbook.temperature_max +
      " Min: " + logbook.temperature_min + "\n" + "Max Speed: 41 mph\n" +
      "Distance (mi) Direct: 2.3 Path: 35.1\n";

  Serial.println("finished");

  return trackdescription;
}