#pragma once

#include <cstdint>

struct Temperature {
  // Stored in 100ths of a degree
  uint32_t raw_value;

  /// @brief Creates a temperature
  /// @param value 100ths of a degree celsius
  explicit Temperature(uint32_t value) : raw_value(value) {}

  double celsius() const { return static_cast<double>(raw_value) / 100.0; }

  double fahrenheit() const { return (celsius() * 9.0 / 5.0) + 32.0; }
};