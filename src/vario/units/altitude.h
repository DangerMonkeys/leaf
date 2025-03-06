#pragma once

/// @brief Represents an altitude measurement with multiple unit conversions
struct Altitude {
  double meters;  ///< Altitude in meters

  /// @brief Creates an altitude measurement
  /// @param m Altitude in meters
  explicit Altitude(double m) : meters(m) {}

  /// @brief Converts altitude to feet
  /// @return Altitude in feet
  double feet() const { return meters * 3.28084; }

  /// @brief Converts altitude to kilometers
  /// @return Altitude in kilometers
  double kilometers() const { return meters / 1000.0; }

  /// @brief Converts altitude to miles
  /// @return Altitude in miles
  double miles() const { return meters * 0.000621371; }
};