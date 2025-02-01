
#include "log.h"

#include <Arduino.h>

#include "IMU.h"
#include "SDcard.h"
#include "baro.h"
#include "gps.h"
#include "logbook/flight.h"
#include "logbook/kml.h"
#include "logbook/igc.h"
#include "settings.h"
#include "speaker.h"
#include "string_utils.h"
#include "tempRH.h"
#include "wind_estimate/wind_estimate.h"


// track if we're "flying" separate from if we're recording a log.  This allows us to enable
// certain in-flight features (like vario quiet_mode) without having to be recording a log.
bool weAreFlying = false; 
bool getAreWeFlying() {
  return weAreFlying;
}

// Local variables

// This file keeps track of logging a flight to persistent storage.  As the operator may
// wish to start their flight and flight log before we have a "fix", know the time or
// location, we keep track of their intent to log, and start the flight record when we have
// a fix and a date/time record.
Flight* flight =
    NULL;  // Pointer to the current flight record (null if we're not deisred to be logging)
Kml kmlFlight;
Igc igcFlight;


// TODO:  Delete ME

// used to keep track of current flight statistics
FlightStats logbook;

//////////////////////////////////////////////////////////////////////////////////////////
// UPDATE - Main function to run every second
void log_update() {

  // Check auto-start criteria if we haven't begun a flight yet
  if (!flight && !weAreFlying) {
    // If auto start is configured, and we match the criteria, start the flight
    if (flightTimer_autoStart()) {
      weAreFlying = true; // if we meet the flying conditions, we're flying!
      if (AUTO_START) flightTimer_start(); // start a log if auto-start is on
    } else {
      // Otherwise, there's nothing to do here.
      return;
    }
  // Check auto-stop criteria if we ARE flying
  } else if (weAreFlying) {
    if (flightTimer_autoStop()) {      
      if (AUTO_STOP) flightTimer_stop();  // stop the log if auto-stop is on
    }
  }

  // Now do all the regular log stuff if we have an active log
  if (flight) {
    // Current second of the flight
    auto currentSecondSinceBoot = millis() / 1000;

    // Current second of the flight
    logbook.duration = currentSecondSinceBoot - logbook.logStartedAt;

    // We wish to log a flight, but the log has not yet started
    if (!flight->started()) {
      if (!gps.location.isValid())
        // We don't have a valid GPS location yet, try again later
        return;

      if (ALT_SYNC_GPS)
        settings_matchGPSAlt();  // sync pressure alt to GPS alt when log starts if the auto-sync
                                // setting is turned on

      // We have a GPS fix, we're able to start recording of the flight.
      // TODO:  A second sound effect to show that recording has now started??
      flight->startFlight();
      // TODO:  Make this sound much cooler
      speaker_playSound(fx_buttonhold);
    }

    // Generate a record to log
    flight->log(logbook.duration);
    log_captureValues();      // TODO:  Update this to an "Update Flight Stats" or something
    log_checkMinMaxValues();  // TODO:  Probably rename this to be "bound Flight Stats"
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
bool flightTimer_isRunning() { return flight != NULL; }
bool flightTimer_isLogging() { return flightTimer_isRunning() && (bool)flight->started(); }

// start timer
void flightTimer_start() {
  weAreFlying = true;  // we're "flying" when we start a log
  // Short-circuit if a flight is already started
  if (flight != NULL) {
    return;
  }

  // start timer
  speaker_playSound(fx_enter);
  switch (LOG_FORMAT) {
    case LOG_FORMAT_KML:
      flight = &kmlFlight;
      break;
    case LOG_FORMAT_IGC:
      flight = &igcFlight;
      break;
    default:
      return;  // DO not start the flight if it's an unknown format
  }

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
  logbook.logStartedAt = millis() / 1000;

  // Finally, save current new altitude as auto-stop altitude
  autoStopAltitude = baro.alt;
}

// stop timer
void flightTimer_stop() {
  weAreFlying = false;  // we're not "flying" when we stop a log
  clearWindEstimate();  // clear the wind estimate when we stop a flight
  // Short Circuit, no need to do anything if there's no flight recording.
  if (flight == NULL) {
    return;
  }

  // ending values
  logbook.alt_end = baro.alt;
  flight->end(logbook);
  // TODO:  A much cooler end flight sound.  Perhaps even an easter egg?
  speaker_playSound(fx_confirm);
  flight = NULL;
  logbook = FlightStats();  // Reset the flight stats
  // TODO:  Pop open a "flight complete" dialog
  
}

void flightTimer_toggle() {
  if (flight)
    flightTimer_stop();
  else
    flightTimer_start();
}

String flightTimer_getString() {
  // If the flight has not yet started logging, just make it flash empty every half second.
  if (flightTimer_isRunning() && !flightTimer_isLogging()) {
    if ((millis() / 500) % 2) return "";
  }
  // Assume that the flight Timer's boxes are always 6 characters wide
  return formatSeconds(logbook.duration, true, 6);
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
