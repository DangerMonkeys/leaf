#pragma once

namespace heap_monitor {

  void record(const char* event);
  bool dumpToSd(const char* path = "/diagnostics/heap_monitor.csv");
  void clear();

}  // namespace heap_monitor
