#pragma once

#include <stdint.h>

#include "ids.h"

class WaypointID : public GenericID<WaypointID, uint8_t> {
 public:
  using GenericID::GenericID;  // Inherit constructor

  WaypointID() : GenericID(WaypointID::None) {}

  // TODO: Remove this implicit cast to better constrain behavior
  operator uint8_t() const { return id_; }

  // This sentinel value indicates an absence of a waypoint.
  static const WaypointID None;
};

class RouteID : public GenericID<RouteID, uint8_t> {
 public:
  using GenericID::GenericID;  // Inherit constructor

  RouteID() : GenericID(RouteID::None) {}

  // TODO: Remove this implicit cast to better constrain behavior
  operator uint8_t() const { return id_; }

  // This "unrouted" route consists of individual waypoints not necessarily meant to be followed in
  // sequence.
  static const RouteID Unrouted;

  // This sentinel value indicates an absence of a route.
  static const RouteID None;
};

/// @brief One-based index of a point within a route.
class RouteIndex {
 public:
  RouteIndex(int16_t value) : value_(value) {}
  RouteIndex() : value_(RouteIndex::None) {}

  // TODO: Remove this implicit cast to better constrain behavior
  operator int16_t() const { return value_; }

  RouteIndex& operator=(const int16_t& newValue) {
    value_ = newValue;
    return *this;
  }

  RouteIndex& operator++() {
    *this = value_ + 1;
    return *this;
  }

  RouteIndex operator++(int) {
    RouteIndex temp = *this;
    *this = value_ + 1;
    return temp;
  }

  // This sentinel value indicates an absence of a route index.
  static const RouteIndex None;

  // This sentinel value indicates no next point on a route.
  static const RouteIndex NoNextPoint;

  // Maximum RouteIndex (a route may not have more than this number of points).
  static const int16_t Max = 12;

 private:
  int16_t value_;
};
