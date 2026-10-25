#pragma once
#if defined(LEAF_TRAINING_STUDENT) || defined(LEAF_TRAINING_INSTRUCTOR)
#include <Arduino.h>
#include <Preferences.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
namespace training {
  struct Config {
    Preferences store;
    uint32_t group = 0, instructor = 0;
    uint8_t slot = 0;
    void begin() {
      store.begin("training-v1", false);
      group = store.getUInt("group", 0);
      instructor = store.getUInt("instructor", 0);
      slot = store.getUChar("slot", 0);
      if (slot >= 4) slot = 0;
    }
    void setGroup(uint32_t value) {
      group = value;
      instructor = 0;
      store.putUInt("group", group);
      store.putUInt("instructor", 0);
    }
  };
  class Console {
   public:
    const char* read() {
      unsigned budget = 32;
      while (budget-- && Serial.available()) {
        char ch = Serial.read();
        if (ch == '\r') continue;
        if (ch == '\n') {
          buffer_[used_] = 0;
          used_ = 0;
          if (overflow_) {
            overflow_ = false;
            continue;
          }
          return buffer_;
        }
        if (used_ < sizeof(buffer_) - 1)
          buffer_[used_++] = ch;
        else
          overflow_ = true;
      }
      return nullptr;
    }

   private:
    char buffer_[48]{};
    unsigned used_ = 0;
    bool overflow_ = false;
  };
  inline bool argument(const char* line, const char* name, uint32_t& value) {
    size_t n = strlen(name);
    if (strncmp(line, name, n) || line[n] != ' ' || line[n + 1] < '0' || line[n + 1] > '9')
      return false;
    char* end = nullptr;
    errno = 0;
    unsigned long v = strtoul(line + n + 1, &end, 0);
    if (!end || *end || errno == ERANGE || uint64_t(v) > UINT32_MAX) return false;
    value = uint32_t(v);
    return true;
  }
}  // namespace training
#endif
