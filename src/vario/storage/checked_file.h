#pragma once

#include <ArduinoJson.h>
#include <cstdio>

// Small checked writers for occasional user-file saves, not continuous flight logging.
namespace checked_file {
  class Writer {
   public:
    explicit Writer(const String& path);
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    explicit operator bool() const { return file_ != nullptr; }
    size_t write(uint8_t byte) { return write(&byte, 1); }
    size_t write(const uint8_t* bytes, size_t count);
    // Checks buffered I/O, close, then reads back length and a streaming checksum.
    bool finish(const String& path);

   private:
    FILE* file_ = nullptr;
    size_t bytes_ = 0;
    uint32_t hash_ = 2166136261U;
    bool failed_ = false;
  };

  bool recover(const String& path);
  bool erase(const String& path);
  bool replaceJson(const String& path, const JsonDocument& document, bool pretty = false);
  bool replaceBytes(const String& path, const String& bytes);

#ifdef LEAF_SIM
  using FaultHook = int (*)(const char* stage);
  void setFaultHook(FaultHook hook);
#endif
}  // namespace checked_file
