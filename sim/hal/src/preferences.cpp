// Preferences (NVS) persisted as one JSON-ish file per namespace.
//
// Deliberately a plain key=value text format rather than a JSON library dependency: the file is
// meant to be readable and hand-editable between emulator runs, and settings values are all
// scalars or short strings.

#include <Preferences.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <fstream>
#include <sstream>

namespace {

  std::string g_statePath = "sim/state";

  std::string fileFor(const std::string& ns) { return g_statePath + "/nvs_" + ns + ".txt"; }

  std::string escape(const std::string& value) {
    std::string out;
    for (char c : value) {
      if (c == '\n') {
        out += "\\n";
      } else if (c == '\\') {
        out += "\\\\";
      } else {
        out += c;
      }
    }
    return out;
  }

  std::string unescape(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); i++) {
      if (value[i] == '\\' && i + 1 < value.size()) {
        out += value[++i] == 'n' ? '\n' : value[i];
      } else {
        out += value[i];
      }
    }
    return out;
  }

}  // namespace

void Preferences::simSetStatePath(const std::string& path) { g_statePath = path; }

void Preferences::simEraseAll() {
  // Namespaces are discovered by filename, so removing the files is a complete erase.
  DIR* dir = opendir(g_statePath.c_str());
  if (!dir) return;
  while (struct dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name.rfind("nvs_", 0) == 0) ::remove((g_statePath + "/" + name).c_str());
  }
  closedir(dir);
}

bool Preferences::begin(const char* name, bool readOnly, const char* partitionLabel) {
  namespace_ = name ? name : "default";
  readOnly_ = readOnly;
  open_ = true;
  load();
  return true;
}

void Preferences::end() {
  if (open_ && !readOnly_) save();
  open_ = false;
  values_.clear();
}

void Preferences::load() {
  values_.clear();
  std::ifstream in(fileFor(namespace_));
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    values_[line.substr(0, eq)] = unescape(line.substr(eq + 1));
  }
}

void Preferences::save() {
  mkdir(g_statePath.c_str(), 0777);
  std::ofstream out(fileFor(namespace_), std::ios::trunc);
  if (!out) return;
  for (const auto& entry : values_) {
    out << entry.first << "=" << escape(entry.second) << "\n";
  }
}

bool Preferences::clear() {
  values_.clear();
  if (!readOnly_) save();
  return true;
}

bool Preferences::remove(const char* key) {
  values_.erase(key ? key : "");
  if (!readOnly_) save();
  return true;
}

bool Preferences::isKey(const char* key) { return values_.count(key ? key : "") > 0; }

size_t Preferences::putChar(const char* key, int8_t value) {
  values_[key] = std::to_string((int)value);
  return 1;
}
size_t Preferences::putUChar(const char* key, uint8_t value) {
  values_[key] = std::to_string((unsigned)value);
  return 1;
}
size_t Preferences::putShort(const char* key, int16_t value) {
  values_[key] = std::to_string((int)value);
  return 2;
}
size_t Preferences::putUShort(const char* key, uint16_t value) {
  values_[key] = std::to_string((unsigned)value);
  return 2;
}
size_t Preferences::putInt(const char* key, int32_t value) {
  values_[key] = std::to_string(value);
  return 4;
}
size_t Preferences::putUInt(const char* key, uint32_t value) {
  values_[key] = std::to_string(value);
  return 4;
}
size_t Preferences::putLong(const char* key, int32_t value) { return putInt(key, value); }
size_t Preferences::putULong(const char* key, uint32_t value) { return putUInt(key, value); }
size_t Preferences::putFloat(const char* key, float value) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%.9g", (double)value);
  values_[key] = buf;
  return 4;
}
size_t Preferences::putDouble(const char* key, double value) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%.17g", value);
  values_[key] = buf;
  return 8;
}
size_t Preferences::putBool(const char* key, bool value) {
  values_[key] = value ? "1" : "0";
  return 1;
}
size_t Preferences::putString(const char* key, const char* value) {
  values_[key] = value ? value : "";
  return strlen(value ? value : "");
}
size_t Preferences::putString(const char* key, const String& value) {
  return putString(key, value.c_str());
}
size_t Preferences::putBytes(const char* key, const void* value, size_t len) {
  values_[key] = std::string((const char*)value, len);
  return len;
}

int8_t Preferences::getChar(const char* key, int8_t defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : (int8_t)atoi(it->second.c_str());
}
uint8_t Preferences::getUChar(const char* key, uint8_t defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : (uint8_t)atoi(it->second.c_str());
}
int16_t Preferences::getShort(const char* key, int16_t defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : (int16_t)atoi(it->second.c_str());
}
uint16_t Preferences::getUShort(const char* key, uint16_t defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : (uint16_t)atoi(it->second.c_str());
}
int32_t Preferences::getInt(const char* key, int32_t defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : (int32_t)strtol(it->second.c_str(), nullptr, 10);
}
uint32_t Preferences::getUInt(const char* key, uint32_t defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : (uint32_t)strtoul(it->second.c_str(), nullptr, 10);
}
int32_t Preferences::getLong(const char* key, int32_t defaultValue) {
  return getInt(key, defaultValue);
}
uint32_t Preferences::getULong(const char* key, uint32_t defaultValue) {
  return getUInt(key, defaultValue);
}
float Preferences::getFloat(const char* key, float defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : strtof(it->second.c_str(), nullptr);
}
double Preferences::getDouble(const char* key, double defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : strtod(it->second.c_str(), nullptr);
}
bool Preferences::getBool(const char* key, bool defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : it->second == "1";
}
String Preferences::getString(const char* key, const String& defaultValue) {
  auto it = values_.find(key ? key : "");
  return it == values_.end() ? defaultValue : String(it->second);
}
size_t Preferences::getBytes(const char* key, void* buf, size_t maxLen) {
  auto it = values_.find(key ? key : "");
  if (it == values_.end()) return 0;
  const size_t n = it->second.size() < maxLen ? it->second.size() : maxLen;
  memcpy(buf, it->second.data(), n);
  return n;
}
