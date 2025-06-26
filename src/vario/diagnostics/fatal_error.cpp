#include "fatal_error.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <stdarg.h>

#include "FS.h"

File fatal_error_file;

bool useFile() {
  if (fatal_error_file) {
    return true;
  }

  unsigned int i = 1;
  char fileName[32];

  while (true) {
    snprintf(fileName, sizeof(fileName), "/fatal_error_%u.txt", i);

    fs::File f = SD_MMC.open(fileName, "r");
    if (!f) {
      // If the error was that the file doesn't exist, break and use this name
      break;
    }

    f.close();  // File exists, close and try next
    i++;
    if (i > 1000) {
      // Avoid infinite loop in case of filesystem issues
      return false;
    }
  }

  fatal_error_file = SD_MMC.open(fileName, "w", true);  // open for writing, create if doesn't exist
  return fatal_error_file;
}

void fatalErrorInfo(const char* msg, ...) {
  constexpr size_t bufferSize = 512;
  char buffer[bufferSize];

  va_list args;
  va_start(args, msg);
  vsnprintf(buffer, bufferSize, msg, args);
  va_end(args);

  Serial.println(buffer);
  if (useFile()) {
    fatal_error_file.println(buffer);
  }
}

void fatalError(const char* msg, ...) {
  Serial.print("FATAL ERROR: ");

  constexpr size_t bufferSize = 512;
  char buffer[bufferSize];

  va_list args;
  va_start(args, msg);
  vsnprintf(buffer, bufferSize, msg, args);
  va_end(args);

  Serial.println(buffer);
  if (useFile()) {
    fatal_error_file.println(buffer);
  }

  if (fatal_error_file) {
    fatal_error_file.close();
  }

  // TODO: when possible, show death icon on LCD along with message
  // TODO: when possible, play death sound

  while (true);
}
