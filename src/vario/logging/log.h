#pragma once

// Module is responsible for logging a flight

#include <Arduino.h>
#include "etl/message_bus.h"

#include "logbook/flight.h"
#include "logging/flight_start_history.h"

struct Waypoint;

// Auto-start tuning lives in logging/flight_auto_start.h.
// Auto Stop Timer Thresholds

#define AUTO_STOP_MAX_SPEED 3     // mph max -- must be below this speed for timer to auto-stop
#define AUTO_STOP_MAX_ACCEL 0.2f  // Max accelerometer signal (m/s^2) to consider "not moving"
#define AUTO_STOP_MAX_ALT 100     // cm altitude change for timer auto-stop
#define AUTO_STOP_MIN_SEC 20      // seconds of low speed / low accel for timer to auto-stop

// Main Log functions
void log_update(void);  // Update function to run every second

// Flight Timer Auto Start/Stop check functions
bool flightTimer_autoStart(void);
void flightTimer_resetAutoStart(void);
bool flightTimer_autoStop(void);

// Flight Timer functions
void flightTimer_start(FlightStartCause cause = FlightStartCause::Manual);
enum class FlightEndCause : uint8_t { Manual, Automatic, PowerOff, LowBattery };
void flightTimer_stop(bool showSummary = true, FlightEndCause cause = FlightEndCause::Manual);
void flightTimer_toggle(void);
bool flightTimer_ownsLogbookPath(const String& path);
bool flightTimer_isRunning(void);  // If the timer is running
bool flightTimer_isLogging(void);  // If the flight recorder log is logging
void flightTimer_markSavedPoint(const Waypoint& waypoint);
void flightTimer_updateStrings(void);

// Returns a short human readable string to represent the flight time.  This is either
// M:SS, or HH:MM depending on how long the flight has been.
// If we've started, but am not yet logging, we'll flash this
String flightTimer_getString();

String log_createTrackFileName(void);

void log_captureValues(void);
void log_checkMinMaxValues(void);
void log_captureEndingValues(void);

// Returns if we should start recording a flight based on movemnt
bool flightTimer_autoStop(void);
uint8_t flightTimer_getAutoStopCountRemaining(void);
void flightTimer_resetAutoStop(void);

// Returns if we should stop recording a flight based on idle-ness
bool flightTimer_autoStart(void);

class IgcLogger;
void log_writeStartDiagnostics(IgcLogger& logger);

extern FlightStats logbook;
