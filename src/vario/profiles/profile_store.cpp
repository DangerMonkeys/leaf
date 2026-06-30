#include "profile_store.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>

namespace {
  String optionalString(JsonVariantConst value) {
    if (value.isNull()) return "";
    return value.as<String>();
  }

  String joinGliderName(const String& brand, const String& model, const String& size) {
    String name;
    if (!brand.isEmpty()) name += brand;
    if (!model.isEmpty()) {
      if (!name.isEmpty()) name += " ";
      name += model;
    }
    if (!size.isEmpty()) {
      if (!name.isEmpty()) name += " ";
      name += size;
    }
    return name;
  }

  PilotProfile pilotFromJson(JsonObjectConst obj) {
    PilotProfile pilot;
    pilot.id = optionalString(obj["id"]);
    pilot.name = optionalString(obj["name"]);
    pilot.email = optionalString(obj["email"]);
    pilot.leafLogApiKey = optionalString(obj["leaf_log_api_key"]);
    return pilot;
  }

  GliderProfile gliderFromJson(JsonObjectConst obj) {
    GliderProfile glider;
    glider.id = optionalString(obj["id"]);
    glider.brand = optionalString(obj["brand"]);
    glider.model = optionalString(obj["model"]);
    if (glider.model.isEmpty()) {
      glider.model = optionalString(obj["name"]);
    }
    glider.size = optionalString(obj["size"]);
    glider.displayName = optionalString(obj["display_name"]);
    return glider;
  }

  bool loadProfiles(JsonDocument& doc) {
    File file = SD_MMC.open(ProfileStore::filePath(), "r");
    if (!file) return false;

    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    const char* schema = doc["schema"] | "";
    return strcmp(schema, "leaf.profiles") == 0;
  }
}  // namespace

String GliderProfile::resolvedDisplayName() const {
  if (!displayName.isEmpty()) return displayName;
  return profileName();
}

String GliderProfile::profileName() const { return joinGliderName(brand, model, size); }

bool ProfileStore::activePilot(PilotProfile& pilot) {
  pilot = PilotProfile();

  JsonDocument doc;
  if (!loadProfiles(doc)) return false;

  JsonArrayConst pilots = doc["pilots"].as<JsonArrayConst>();
  if (pilots.isNull()) return false;

  const String activeId = optionalString(doc["active_pilot_id"]);
  PilotProfile onlyPilot;
  uint16_t validPilotCount = 0;

  for (JsonObjectConst item : pilots) {
    PilotProfile candidate = pilotFromJson(item);
    if (!candidate.valid()) continue;

    validPilotCount++;
    onlyPilot = candidate;
    if (!activeId.isEmpty() && candidate.id == activeId) {
      pilot = candidate;
      return true;
    }
  }

  if (activeId.isEmpty() && validPilotCount == 1) {
    pilot = onlyPilot;
    return true;
  }

  return false;
}

bool ProfileStore::activeGlider(GliderProfile& glider) {
  glider = GliderProfile();

  JsonDocument doc;
  if (!loadProfiles(doc)) return false;

  JsonArrayConst gliders = doc["gliders"].as<JsonArrayConst>();
  if (gliders.isNull()) return false;

  const String activeId = optionalString(doc["active_glider_id"]);
  GliderProfile onlyGlider;
  uint16_t validGliderCount = 0;

  for (JsonObjectConst item : gliders) {
    GliderProfile candidate = gliderFromJson(item);
    if (!candidate.valid()) continue;

    validGliderCount++;
    onlyGlider = candidate;
    if (!activeId.isEmpty() && candidate.id == activeId) {
      glider = candidate;
      return true;
    }
  }

  if (activeId.isEmpty() && validGliderCount == 1) {
    glider = onlyGlider;
    return true;
  }

  return false;
}
