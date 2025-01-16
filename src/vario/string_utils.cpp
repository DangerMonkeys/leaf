#include "string_utils.h"

String formatSeconds(unsigned long seconds,
                     const bool minified,
                     const int rightAlignWidth) {
  unsigned long hours = seconds / 3600;
  seconds %= 3600;
  unsigned long minutes = seconds / 60;
  seconds %= 60;

  char buffer[9];  // Buffer for "HH:MM:SS\0"
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
  sprintf(buffer, ((String)"%0" + numDigits + "d").c_str(), src);
  return buffer;
}