#pragma once

#include <cmath>
#include <cstdint>

#include "altitude.h"

/// @brief Represents pressure (in 100ths of millibars hPa)
struct Pressure {
  // Raw value in 100ths of millibars (hPa)
  int32_t raw_value;

  explicit Pressure(int32_t value) : raw_value(value) {}
  explicit Pressure() : raw_value(0) {}

  // Assignment operator to assign int32_t to Pressure
  Pressure& operator=(int32_t value) {
    raw_value = value;
    return *this;
  }

  // Type conversion operator to convert Pressure to int32_t
  operator int32_t() const { return raw_value; }

  // Addition assignment operator
  Pressure& operator+=(int32_t value) {
    raw_value += value;
    return *this;
  }

  // Subtraction assignment operator
  Pressure& operator-=(int32_t value) {
    raw_value -= value;
    return *this;
  }

  // Multiplication assignment operator
  Pressure& operator*=(int32_t value) {
    raw_value *= value;
    return *this;
  }

  // Division assignment operator
  Pressure& operator/=(int32_t value) {
    raw_value /= value;
    return *this;
  }

  /// @brief hPa
  /// @return
  double millibars() const { return static_cast<double>(raw_value) / 100.0; }

  static Pressure fromMillibars(float mBar) { return Pressure((int32_t)(mBar * 100)); }

  /// @brief pA
  /// @return
  double pascals() const { return millibars() * 100.0; }

  /// @brief inHg
  /// @return
  double inchesOfMercury() const { return millibars() * 0.02953; }

  /// @brief Calculates altitude using the barometric formula
  /// @return Altitude object representing the computed altitude
  Altitude altitude() const {
    constexpr double sea_level_pressure = 1013.25;  // Standard sea level pressure in hPa
    constexpr double scale_height = 44330.0;        // Approximate scale height in meters
    double meters = scale_height * (1.0 - std::pow(millibars() / sea_level_pressure, 0.1903));
    return Altitude(meters);
  }
};
