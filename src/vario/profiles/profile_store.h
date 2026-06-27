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
  String resolvedDisplayName() const;
};

class ProfileStore {
 public:
  static constexpr const char* directoryPath() { return "/profiles"; }
  static constexpr const char* filePath() { return "/profiles/profiles.json"; }

  static bool activePilot(PilotProfile& pilot);
  static bool activeGlider(GliderProfile& glider);
};
