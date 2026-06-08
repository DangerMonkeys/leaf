#pragma once

#include <atomic>

#include "FirmwareMSC.h"
#include "USBMSC.h"

class SDCard {
 public:
  // One-off SD card (re)format state, driven by requestFormat().
  enum class FormatState : uint8_t { Idle, Running, Success, Failed };

  void init();
  void update();

  bool mount();
  bool isMounted() { return mounted_; }

  bool setupMassStorage();
  static bool isCardPresent();

  // Request a reformat of the SD card. This runs synchronously and is only intended for the rare
  // corrupt/unformatted commissioning case. Callers are responsible for any authorization gating
  // (see SelfTest::allowReformattingOfSDcard()).
  void requestFormat();
  FormatState formatState() const { return format_state_.load(); }
  const char* formatMessage() const { return format_message_.load(); }

 private:
  void runFormat();

  // Whether the SD card is currently mounted (used to compare against the SD_DETECT
  // pin so we can tell if a card has been inserted or removed)
  bool mounted_ = false;

  // Format status exposed to the webserver as a simple polling-friendly state/message pair.
  std::atomic<FormatState> format_state_{FormatState::Idle};
  std::atomic<const char*> format_message_{""};

  FirmwareMSC firmwareMSC_;
  USBMSC msc_;
};

extern SDCard sdcard;
