#include "profile_store.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <string.h>

#include "diagnostics/heap_monitor.h"

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
    heap_monitor::checkpoint("profiles-load-start");
    if (!SD_MMC.exists(ProfileStore::filePath())) {
      heap_monitor::checkpoint("profiles-load-missing");
      return true;
    }

    File file = SD_MMC.open(ProfileStore::filePath(), "r");
    if (!file) {
      heap_monitor::checkpoint("profiles-load-open-fail");
      return false;
    }

    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
      heap_monitor::checkpoint("profiles-load-json-fail");
      return false;
    }

    const char* schema = doc["schema"] | "";
    const bool valid = strcmp(schema, "leaf.profiles") == 0;
    heap_monitor::checkpoint(valid ? "profiles-load-end" : "profiles-load-schema-fail");
    return valid;
  }

  bool profileIdExists(JsonArrayConst profiles, const String& id) {
    if (id.isEmpty()) return false;

    for (JsonObjectConst item : profiles) {
      const String candidateId = optionalString(item["id"]);
      if (candidateId == id) return true;
    }

    return false;
  }

  bool ensureProfileDirectory() {
    File existing = SD_MMC.open(ProfileStore::directoryPath());
    if (existing) {
      const bool isDirectory = existing.isDirectory();
      existing.close();
      return isDirectory;
    }

    if (!SD_MMC.mkdir(ProfileStore::directoryPath())) return false;

    File created = SD_MMC.open(ProfileStore::directoryPath());
    const bool isDirectory = created && created.isDirectory();
    if (created) created.close();
    return isDirectory;
  }

  bool writeProfiles(JsonDocument& doc) {
    heap_monitor::checkpoint("profiles-write-start");
    if (!ensureProfileDirectory()) return false;

    File file = SD_MMC.open(ProfileStore::filePath(), "w");
    if (!file) {
      heap_monitor::checkpoint("profiles-write-open-fail");
      return false;
    }

    const size_t written = serializeJson(doc, file);
    file.close();
    const bool ok = written > 0;
    heap_monitor::checkpoint(ok ? "profiles-write-end" : "profiles-write-fail");
    return ok;
  }

  bool selectProfileId(const char* activeKey, const char* arrayKey, const String& id) {
    JsonDocument doc;
    if (!loadProfiles(doc)) return false;

    JsonArrayConst profiles = doc[arrayKey].as<JsonArrayConst>();
    if (profiles.isNull() || !profileIdExists(profiles, id)) return false;

    doc[activeKey] = id;
    return writeProfiles(doc);
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

bool ProfileStore::loadPicker(ProfilePickerList& pilots, ProfilePickerList& gliders) {
  JsonDocument doc;
  if (!loadProfiles(doc)) return false;

  const String activePilotId = optionalString(doc["active_pilot_id"]);
  const String activeGliderId = optionalString(doc["active_glider_id"]);
  for (JsonObjectConst item : doc["pilots"].as<JsonArrayConst>()) {
    if (pilots.count == ProfilePickerList::Capacity) break;
    PilotProfile pilot = pilotFromJson(item);
    if (!pilot.valid()) continue;
    if (!activePilotId.isEmpty() && pilot.id == activePilotId && pilots.selected < 0)
      pilots.selected = pilots.count;
    pilots.entries[pilots.count++] = {std::move(pilot.id), std::move(pilot.name)};
  }
  for (JsonObjectConst item : doc["gliders"].as<JsonArrayConst>()) {
    if (gliders.count == ProfilePickerList::Capacity) break;
    GliderProfile glider = gliderFromJson(item);
    if (!glider.valid()) continue;
    String label = glider.resolvedDisplayName();
    if (label.isEmpty()) return false;
    if (!activeGliderId.isEmpty() && glider.id == activeGliderId && gliders.selected < 0)
      gliders.selected = gliders.count;
    gliders.entries[gliders.count++] = {std::move(glider.id), std::move(label)};
  }
  if (activePilotId.isEmpty() && pilots.count == 1) pilots.selected = 0;
  if (activeGliderId.isEmpty() && gliders.count == 1) gliders.selected = 0;
  return true;
}

bool ProfileStore::selectPilot(const String& id) {
  return selectProfileId("active_pilot_id", "pilots", id);
}

bool ProfileStore::selectGlider(const String& id) {
  return selectProfileId("active_glider_id", "gliders", id);
}
