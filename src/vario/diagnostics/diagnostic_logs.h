#pragma once

#include <Arduino.h>
#include <FS.h>

namespace diagnostic_logs {

  enum class Log : uint8_t { SystemEvents, NetworkEvents, WebRequests, Vario };

  constexpr const char* DIAGNOSTICS_DIR = "/diagnostics";
  constexpr const char* SYSTEM_EVENTS_PATH = "/diagnostics/system_events.csv";
  constexpr const char* NETWORK_EVENTS_PATH = "/diagnostics/network_events.csv";
  constexpr const char* WEB_REQUESTS_PATH = "/diagnostics/web_requests.csv";
  constexpr const char* VARIO_PATH = "/diagnostics/vario.csv";

  bool enabled(Log log);
  bool ensureDirectory();
  void printCsvString(File& file, const String& value);

}  // namespace diagnostic_logs
