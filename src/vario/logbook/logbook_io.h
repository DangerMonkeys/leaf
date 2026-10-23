#pragma once

#include <ArduinoJson.h>
#include <mutex>
#include "Arduino.h"

// One narrow transaction boundary for logbook I/O and SD ownership transitions.
namespace logbook_io {
  class Guard {
   public:
    explicit Guard(bool wait = false);
    explicit operator bool() const { return lock_.owns_lock(); }

   private:
    std::unique_lock<std::recursive_mutex> lock_;
  };

  struct Result {
    const char* stage = "ok";
    int error = 0;
    size_t expected = 0;
    size_t actual = 0;
  };
  Result lastResult();
  bool retryable();
  bool ensureDirectory();

  // Callers hold Guard from prepare through commit. Documents are never retained.
  bool prepare(const String& path, uint32_t& nextRevision, uint64_t& previousPayload);
  bool commit(const String& path, JsonDocument& document, uint64_t previousPayload = 0);
  bool recover(const String& path);
  bool load(const String& path, JsonDocument& document);
  bool erase(const String& path, const String& trackPath = "");
  bool relocate(const String& oldPath, const String& newPath);
  // Maps directory entries to one logical flight without mutating a live iterator.
  bool candidatePath(const String& entry, String& path);

#ifdef LEAF_SIM
  // Deterministic faults affect the real save decisions, not an alternate implementation.
  using FaultHook = int (*)(const char* stage);
  void setFaultHook(FaultHook hook);
#endif
}  // namespace logbook_io
