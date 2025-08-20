#include "diagnostics/fatal_error.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <esp_debug_helpers.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <stdarg.h>

#include "hardware/buttons.h"
#include "leaf_version.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"
#include "ui/input/buttons.h"
#include "ui/settings/settings.h"

File fatal_error_file;

portMUX_TYPE btMux = portMUX_INITIALIZER_UNLOCKED;

constexpr size_t BUFFER_SIZE = 512;

namespace putc_interception {
  // state used by the putc hook
  static char* putc_buffer = nullptr;
  static size_t putc_buffer_capacity = 0;
  static size_t putc_buffer_index = 0;

  extern "C" void IRAM_ATTR putc_intercept(char c) {
    if (putc_buffer && putc_buffer_index < putc_buffer_capacity) {
      putc_buffer[putc_buffer_index++] = c;
    }
  }
}  // namespace putc_interception

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

  // Write the version information to know what generated this fatal error
  fatal_error_file.print("Firmware version: ");
  fatal_error_file.println(FIRMWARE_VERSION);

  return fatal_error_file;
}

static void getBacktrace(char* buffer, size_t n) {
  portENTER_CRITICAL(&btMux);

  // Redirect ROM printf to our buffer for the duration of the backtrace print
  putc_interception::putc_buffer = buffer;
  putc_interception::putc_buffer_capacity = n;
  putc_interception::putc_buffer_index = 0;
  esp_rom_install_channel_putc(1, putc_interception::putc_intercept);

  // Trigger backtrace print
  esp_backtrace_print(16);

  // Restore default UART output for ROM printf
  esp_rom_install_uart_printf();

  portEXIT_CRITICAL(&btMux);

  // Null-terminate backtrace string in buffer
  if (putc_interception::putc_buffer_index >= n) {
    putc_interception::putc_buffer_index = n - 1;
  }
  buffer[putc_interception::putc_buffer_index] = 0;
}

void fatalErrorInfo(const char* msg, ...) {
  char buffer[BUFFER_SIZE];

  va_list args;
  va_start(args, msg);
  vsnprintf(buffer, BUFFER_SIZE, msg, args);
  va_end(args);

  Serial.println(buffer);
  if (useFile()) {
    fatal_error_file.print("Info: ");
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
  ButtonEvent button_state;

  // Wait until no buttons are pressed
  do {
    which_button = buttons.inspectPins();
  } while (which_button == Button::NONE);

  // Wait until a button is held
  const unsigned long RESET_HOLD_TIME_MS = 3000;
  unsigned long t0 = millis();
  do {
    Button new_button = buttons.inspectPins();
    if (new_button != which_button) {
      which_button = new_button;
      t0 = millis();
    }
  } while (which_button == Button::NONE || millis() - t0 < RESET_HOLD_TIME_MS);

  speaker.playSound(fx::off);
  while (speaker.update()) {
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
  Serial.flush();

  // Try to write final error message to file
  if (useFile()) {
    fatal_error_file.print("Error: ");
    fatal_error_file.println(buffer);
  }

  // Show fatal error info on screen
  u8g2.clear();
  displayFatalError(buffer);

  // Load backtrace into buffer
  getBacktrace(buffer, BUFFER_SIZE);

  // Report backtrace
  Serial.println(buffer);
  if (useFile()) {
    fatal_error_file.println(buffer);
  }

  // Close file if open
  if (fatal_error_file) {
    fatal_error_file.close();
  }

  // Play fatal error sound
  speaker.unMute();
  settings.system_volume = 3;
  speaker.playSound(fx::fatalerror);
  while (speaker.update()) {
    delay(10);
  }

  rebootOnKeyPress();
}
