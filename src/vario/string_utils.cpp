#include "string_utils.h"

String formatSeconds(unsigned long seconds, const bool minified, const int rightAlignWidth) {
  unsigned long hours = seconds / 3600;
  seconds %= 3600;
  unsigned long minutes = seconds / 60;
  seconds %= 60;

  char buffer[9];            // Buffer for "HH:MM:SS\0"
  char formattedBuffer[20];  // Buffer for right-aligned text

  if (!minified) {
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  } else {
    if (hours)
      snprintf(buffer, sizeof(buffer), "%lu:%02lu", hours, minutes);
    else
      snprintf(buffer, sizeof(buffer), "%lu:%02lu", minutes, seconds);
  }

  if (rightAlignWidth > 0) {
    snprintf(formattedBuffer, sizeof(formattedBuffer), "%*s", rightAlignWidth, buffer);
    return String(formattedBuffer);
  }

  return String(buffer);
}

String toDigits(const int src, const int numDigits) {
  char buffer[10];
  sprintf(buffer, ((String) "%0" + numDigits + "d").c_str(), src);
  return buffer;
}

String formatClimbRate(int32_t climbRate, bool units, bool showUnits) {
  char buffer[8];  // Buffer for "9999fpm\0" fpm or "99.9m/s\0"
  char sign = ' ';
  char unitSymbol = (char)(131 + units);  // add one =132 if fpm
  float climbInMS = 0;

  if (climbRate > 0)
    sign = '+';
  else if (climbRate < 0) {
    sign = '-';
    climbRate *= -1;  // keep positive part
  }

  if (units) {  // fpm
    // convert from cm/s to fpm (lose one significant digit)
    climbRate = climbRate * 197 / 1000 * 10;
    snprintf(buffer, sizeof(buffer), "%4d", climbRate);
  } else {
    // start with cm/s, lose one decimal place and round off in the process
    climbRate = (climbRate + 5) / 10;
    // convert to float for ease of printing with the decimal in place
    climbInMS = (float)climbRate / 10;
    snprintf(buffer, sizeof(buffer), "%2.1f", climbInMS);
  }

  String result = String(sign) + String(buffer);

  if (showUnits) result += String(unitSymbol);

  return result;
}

String formatAlt(int32_t alt, bool units, bool showUnits) {
  char buffer[7];                       // Buffer for "99,999\0" or "-9,999\0"
  char unitSymbol = char(127 + units);  // 'm' or if(units) then 128='ft'

  if (units)
    alt = alt * 100 / 3048;  // convert cm to ft
  else
    alt /= 100;  // convert from cm to m

  // cap max digits
  if (alt > 99999) alt = 99999;
  if (alt < -9999) alt = -9999;

  int8_t thousands = 0;
  if (alt >= 1000) {
    thousands = alt / 1000;  // capture thousands
    alt = alt % 1000;        // save just the hundreds
  }

  if (thousands != 0) {
    snprintf(buffer, sizeof(buffer), "%2d,%03d", thousands, alt);
  } else {
    snprintf(buffer, sizeof(buffer), "%5d", alt);
  }

  String result = String(buffer);
  if (showUnits) result += String(unitSymbol);

  return result;
}

String formatSpeed(float speed, bool units, bool showUnits) {
  char buffer[4];                       // buffer for "999\0"
  char unitSymbol = char(135 + units);  // kph, or if(units) then 136=mph

  if (units)
    speed *= 2.23694f;  // convert to mph
  else
    speed *= 3.6f;  // convert to kph

  snprintf(buffer, sizeof(buffer), "%3d", speed);

  String result = String(buffer);
  if (showUnits) result += String(unitSymbol);

  return result;
}

String formatAccel(float accel, bool showUnits) {
  char buffer[4];  // "3.1\0"
  char unitSymbol = 'g';

  snprintf(buffer, sizeof(buffer), "%1.1f", accel);

  String result = String(buffer);
  if (showUnits) result += String(unitSymbol);

  return result;
}