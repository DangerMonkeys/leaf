#pragma once

#include <esp_err.h>
#include <cstdint>

// Owns ADC1 for both CPU reads and ULP measurements. Call from the device loop.
// The sleep caller supplies its duration, independently of the current power mode.
namespace sleep_adc {
  struct Result {
    uint16_t raw = 0;
    uint16_t milliVolts = 0;
    uint16_t samples = 0;
    esp_err_t error = ESP_OK;
  };

  bool readMilliVolts(uint8_t pin, uint16_t& milliVolts);
  // Schedule one conversion halfway through the planned sleep, then immediately power ADC off.
  // Pair every prepare() with finish(), including failed/rejected sleep attempts.
  esp_err_t prepare(uint8_t pin, uint32_t plannedSleepUs);
  // Accept samples only when the caller's measurement window was valid.
  Result finish(bool acceptSamples, uint32_t elapsedUs);
}  // namespace sleep_adc
