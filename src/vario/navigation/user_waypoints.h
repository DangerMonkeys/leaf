#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "navigation/gpx.h"

namespace user_waypoints {

  constexpr const char* filePath() { return "/waypoints/user_waypoints.json"; }

  bool appendCurrentPosition(Waypoint& savedWaypoint, String& error);
  bool hasSavedPoints();
  bool loadIntoNavigator();
  bool loadAsNavigatorSource(bool persist = true);
  bool appendJsonList(String& json, String& error);
  bool renameFromJson(JsonDocument& input, String& error);
  bool deleteById(const char* id, String& error);

}  // namespace user_waypoints
