#pragma once

#include <esp_err.h>
#include <cstdint>

// Owns ADC1 for both CPU reads and ULP measurements. Call from the device loop.
// The sleep caller supplies its duration, independently of the current power mode.
namespace sleep_adc {
  constexpr unsigned BURST_CONVERSIONS = 9;  // First discarded conversion, then eight averaged.
  enum class Mode { Burst, LongSettle, Single };
  struct Result {
    uint16_t raw = 0;
    uint16_t milliVolts = 0;
    uint16_t firstMilliVolts = 0;
    uint16_t samples = 0;
    esp_err_t error = ESP_OK;
    uint16_t settleCycles = 0;
    uint16_t conversions[BURST_CONVERSIONS] = {};
  };

  struct AwakeCheck {
    uint16_t cpuBeforeMv = 0;
    uint16_t cpuAfterMv = 0;
    Result ulp;
    esp_err_t error = ESP_OK;
  };

  bool readMilliVolts(uint8_t pin, uint16_t& milliVolts);
  // Schedule one acquisition halfway through the planned sleep. Single uses the first
  // conversion and immediately powers ADC off; Burst discards the first, then averages eight.
  // Pair every prepare() with finish(), including failed/rejected sleep attempts.
  esp_err_t prepare(uint8_t pin, uint32_t plannedSleepUs, Mode mode = Mode::Single);
  // Accept samples only when the caller's measurement window was valid.
  Result finish(bool acceptSamples, uint32_t elapsedUs);
  // Bench comparison: keep the CPU busy while the normal single acquisition runs.
  AwakeCheck checkWhileAwake(uint8_t pin);
}  // namespace sleep_adc
