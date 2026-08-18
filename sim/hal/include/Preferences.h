// ESP32 Preferences (NVS) for the host emulator.
//
// Persisted as JSON under the emulator state directory, one object per namespace, so saved
// settings survive a restart of the emulator exactly as they survive a power cycle on device --
// and so you can read or hand-edit them between runs.
#pragma once

#include <math.h>
#include <stdint.h>

#include <map>
#include <string>

#include "WString.h"

class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false, const char* partitionLabel = nullptr);
  void end();

  bool clear();
  bool remove(const char* key);
  bool isKey(const char* key);

  size_t putChar(const char* key, int8_t value);
  size_t putUChar(const char* key, uint8_t value);
  size_t putShort(const char* key, int16_t value);
  size_t putUShort(const char* key, uint16_t value);
  size_t putInt(const char* key, int32_t value);
  size_t putUInt(const char* key, uint32_t value);
  size_t putLong(const char* key, int32_t value);
  size_t putULong(const char* key, uint32_t value);
  size_t putFloat(const char* key, float value);
  size_t putDouble(const char* key, double value);
  size_t putBool(const char* key, bool value);
  size_t putString(const char* key, const char* value);
  size_t putString(const char* key, const String& value);
  size_t putBytes(const char* key, const void* value, size_t len);

  int8_t getChar(const char* key, int8_t defaultValue = 0);
  uint8_t getUChar(const char* key, uint8_t defaultValue = 0);
  int16_t getShort(const char* key, int16_t defaultValue = 0);
  uint16_t getUShort(const char* key, uint16_t defaultValue = 0);
  int32_t getInt(const char* key, int32_t defaultValue = 0);
  uint32_t getUInt(const char* key, uint32_t defaultValue = 0);
  int32_t getLong(const char* key, int32_t defaultValue = 0);
  uint32_t getULong(const char* key, uint32_t defaultValue = 0);
  float getFloat(const char* key, float defaultValue = NAN);
  double getDouble(const char* key, double defaultValue = NAN);
  bool getBool(const char* key, bool defaultValue = false);
  String getString(const char* key, const String& defaultValue = String());
  size_t getBytes(const char* key, void* buf, size_t maxLen);
  size_t freeEntries() { return 1000; }

  // Wipes every namespace; backs nvs_flash_erase().
  static void simEraseAll();
  static void simSetStatePath(const std::string& path);

 private:
  void load();
  void save();

  std::string namespace_;
  bool open_ = false;
  bool readOnly_ = false;
  std::map<std::string, std::string> values_;
};
