#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>

#include "navigation/gpx.h"

namespace user_waypoints {

  constexpr const char* filePath() { return "/waypoints/user_waypoints.json"; }

  bool validPoint(JsonObjectConst point);

  // Validate the entire file before callers send a response or modify navigation state.
  class Reader {
   public:
    bool open(String& error);
    bool hasValidPoint() const { return hasValidPoint_; }
    bool next(JsonDocument& point);
    bool failed() const { return failed_; }

   private:
    File file_;
    bool hasValidPoint_ = false;
    bool readingPoints_ = false;
    bool failed_ = false;
  };

  bool appendCurrentPosition(Waypoint& savedWaypoint, String& error);
  bool hasSavedPoints();
  bool loadIntoNavigator();
  bool loadAsNavigatorSource(bool persist = true);
  bool renameFromJson(JsonDocument& input, String& error);
  bool deleteById(const char* id, String& error);

}  // namespace user_waypoints
