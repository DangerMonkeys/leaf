#pragma once

#include <Arduino.h>

struct PilotProfile {
  String id;
  String name;

  bool valid() const { return !id.isEmpty() && !name.isEmpty(); }
};

struct GliderProfile {
  String id;
  String brand;
  String model;
  String size;
  String displayName;

  bool valid() const { return !id.isEmpty() && !model.isEmpty(); }
  String profileName() const;
  String resolvedDisplayName() const;
};

class ProfileStore {
 public:
  static constexpr const char* directoryPath() { return "/profiles"; }
  static constexpr const char* filePath() { return "/profiles/profiles.json"; }

  static bool activePilot(PilotProfile& pilot);
  static bool activeGlider(GliderProfile& glider);
  static bool load(PilotProfile* pilots, size_t maxPilots, size_t& pilotCount,
                   GliderProfile* gliders, size_t maxGliders, size_t& gliderCount,
                   String& activePilotId, String& activeGliderId);
  static bool selectPilot(const String& id);
  static bool selectGlider(const String& id);
};
