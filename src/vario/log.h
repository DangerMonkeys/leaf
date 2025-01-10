#pragma once

// Module is responsible for logging a flight

#include <Arduino.h>
#include "logbook/flight.h"

// Auto Start/Stop Timer Thresholds
#define AUTO_START_MIN_SPEED 5  // mph minimum before timer will auto-start
#define AUTO_START_MIN_SEC 3    // seconds above the min speed to auto-start
#define AUTO_START_MIN_ALT 500  // cm altitude change for timer auto-start

#define AUTO_STOP_MAX_SPEED 3   // mph max -- must be below this speed for timer to auto-stop
#define AUTO_STOP_MAX_ACCEL 10  // Max accelerometer signal
#define AUTO_STOP_MAX_ALT 200   // cm altitude change for timer auto-stop
#define AUTO_STOP_MIN_SEC 10    // seconds of low speed / low accel for timer to auto-stop

// Main Log functions
void log_update(void);  // Update function to run every second

// Flight Timer Auto Start/Stop check functions
bool flightTimer_autoStart(void);
bool flightTimer_autoStop(void);

// Flight Timer functions
void flightTimer_start(void);
void flightTimer_stop(void);
void flightTimer_toggle(void);
bool flightTimer_isRunning(void);  // If the timer is running
bool flightTimer_isLogging(void);  // If the flight recorder log is logging
void flightTimer_updateStrings(void);

// Returns a short human readable string to represent the flight time.  This is either
// M:SS, or HH:MM depending on how long the flight has been.
// If we've started, but am not yet logging, we'll flash this
String flightTimer_getString();

// Log Files
String log_getKMLCoordinates(void);
String log_createTrackFileName(void);

void log_captureValues(void);
void log_checkMinMaxValues(void);

// Returns if we should start recording a flight based on movemnt
bool flightTimer_autoStop(void);

// Returns if we should stop recording a flight based on idle-ness
bool flightTimer_autoStart(void);

extern FlightStats logbook;
