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

struct ProfilePickerEntry {
  String id;
  String label;
};

struct ProfilePickerList {
  static constexpr uint8_t Capacity = 16;
  ProfilePickerEntry entries[Capacity];
  uint8_t count = 0;
  int8_t selected = -1;
};

class ProfileStore {
 public:
  static constexpr const char* directoryPath() { return "/profiles"; }
  static constexpr const char* filePath() { return "/profiles/profiles.json"; }

  static bool activePilot(PilotProfile& pilot);
  static bool activeGlider(GliderProfile& glider);
  static bool loadPicker(ProfilePickerList& pilots, ProfilePickerList& gliders);
  static bool selectPilot(const String& id);
  static bool selectGlider(const String& id);
};
