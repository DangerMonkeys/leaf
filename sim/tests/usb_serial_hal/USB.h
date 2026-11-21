#pragma once
#include <cstdint>

using esp_event_base_t = const char*;
enum {
  ARDUINO_USB_STARTED_EVENT,
  ARDUINO_USB_STOPPED_EVENT,
  ARDUINO_USB_SUSPEND_EVENT,
  ARDUINO_USB_RESUME_EVENT
};
struct TestUSB {
  void onEvent(void (*)(void*, esp_event_base_t, int32_t, void*)) {}
  void manufacturerName(const char*) {}
  void productName(const char*) {}
  void PID(uint16_t) {}
  bool begin() { return true; }
};
inline TestUSB USB;
