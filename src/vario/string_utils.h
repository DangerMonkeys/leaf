#pragma once

#include <Arduino.h>
#include "time.h"

// Formats seconds into HH:MM:SS (or HH:MM if short)
// If rightAlignTo is set, we'll pad this many characters
String formatSeconds(unsigned long seconds,
                     const bool minified = false,
                     const int rightAlignTo = 0);

String toDigits(const int src, const int numDigits);