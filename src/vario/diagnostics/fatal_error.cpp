#include "fatal_error.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <stdarg.h>

#include "FS.h"

#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

File fatal_error_file;

constexpr size_t BUFFER_SIZE = 512;

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
  char buffer[BUFFER_SIZE];

  va_list args;
  va_start(args, msg);
  vsnprintf(buffer, BUFFER_SIZE, msg, args);
  va_end(args);

  Serial.println(buffer);
  if (useFile()) {
    fatal_error_file.println(buffer);
  }
}

void displayFatalError(char* msg) {
  u8g2.firstPage();
  do {
    // TODO: Add skull and cross bones icon

    u8g2.setFont(leaf_6x12);
    u8g2.setCursor(0, 12);
    u8g2.print("FATAL ERROR");

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(0, 36);
    u8g2.print(msg);  // TODO: wrap words to multiple lines

    u8g2.setFont(leaf_5x8);
    u8g2.setCursor(0, 163);
    u8g2.print("Please report this");
    u8g2.setCursor(0, 172);
    u8g2.print("error.");
    u8g2.setCursor(0, 186);
    u8g2.print("Hold button to reboot");
  } while (u8g2.nextPage());

  // TODO: Add QR code linking to error reporting instructions
}

void rebootOnKeyPress() {
  Button which_button;
  ButtonState button_state;

  // Wait until no buttons are pressed
  do {
    which_button = buttons_check();
  } while (which_button == Button::NONE);

  // Wait until a button is held
  do {
    which_button = buttons_check();
    button_state = buttons_get_state();
  } while (button_state != ButtonState::HELD);

  speaker_playSound(fx_off);
  while (onSpeakerTimer()) {
    delay(10);
  }

  ESP.restart();
}

void fatalError(const char* msg, ...) {
  Serial.print("FATAL ERROR: ");

  // Construct final error message
  char buffer[BUFFER_SIZE];
  va_list args;
  va_start(args, msg);
  vsnprintf(buffer, BUFFER_SIZE, msg, args);
  va_end(args);

  Serial.println(buffer);

  // Try to write final error message to file
  if (useFile()) {
    fatal_error_file.println(buffer);
  }

  // Close file if open
  if (fatal_error_file) {
    fatal_error_file.close();
  }

  // Show fatal error info on screen
  u8g2.clear();
  displayFatalError(buffer);

  // Play fatal error sound
  speaker_unMute();
  settings.system_volume = 3;
  speaker_playSound(fx_fatalerror);
  while (onSpeakerTimer()) {
    delay(10);
  }

#ifdef DEBUG_FATALERROR_COREDUMP
  assert(0);  // Force core dump
#else
  rebootOnKeyPress();
#endif
}
