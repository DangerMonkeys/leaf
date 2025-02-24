#pragma once

#include <Arduino.h>
#include "time.h"

// Formats seconds into HH:MM:SS (or HH:MM if short)
// If rightAlignTo is set, we'll pad this many characters
String formatSeconds(unsigned long seconds,
                     const bool minified = false,
                     const int rightAlignTo = 0);

String toDigits(const int src, const int numDigits);

// Formats ClimbRate into proper units with +/- sign
// either +9999fpm or +99.9m/s
String formatClimbRate(int32_t climbRate, bool units, bool showUnits);

// Format Altitude (cm) into proper units with comma.  
// Max digits are either 99,9999 if positive or -9,999 if negative.
String formatAlt(int32_t altInCM, bool units, bool showUnits);

// Format Speed into proper units  
// Max digits 999
String formatSpeed(float speed, bool units, bool showUnits);

// Format accel "1.2g"
String formatAccel(float accel, bool showUnits);