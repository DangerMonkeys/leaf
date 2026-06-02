#pragma once

#include <atomic>

#include "FirmwareMSC.h"
#include "USBMSC.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class SDCard {
 public:
  // Asynchronous SD card (re)format state, driven by requestFormat() and a background task.
  enum class FormatState : uint8_t { Idle, Running, Success, Failed };

  void init();
  void update();

  bool mount();
  bool isMounted() { return mounted_; }

  bool setupMassStorage();
  static bool isCardPresent();

  // Request an asynchronous reformat of the SD card. Spawns a background FreeRTOS task that
  // reformats the card if it cannot be mounted (the corrupt/unformatted case). No-op if a format
  // is already running. Poll formatState()/formatMessage() for progress. Callers are responsible
  // for any authorization gating (see SelfTest::allowReformattingOfSDcard()).
  void requestFormat();
  FormatState formatState() const { return format_state_.load(); }
  const char* formatMessage() const { return format_message_.load(); }

 private:
  void runFormat();
  static void formatTaskEntry(void* arg);

  // Whether the SD card is currently mounted (used to compare against the SD_DETECT
  // pin so we can tell if a card has been inserted or removed)
  bool mounted_ = false;

  // Cross-task format state. Only a status byte and a pointer to a string literal are shared with
  // the main loop, so plain atomics are sufficient (no String, which would not be safe to share).
  std::atomic<FormatState> format_state_{FormatState::Idle};
  std::atomic<const char*> format_message_{""};
  TaskHandle_t format_task_ = nullptr;

  FirmwareMSC firmwareMSC_;
  USBMSC msc_;
};

extern SDCard sdcard;
