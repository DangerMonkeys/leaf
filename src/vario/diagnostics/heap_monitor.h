#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace heap_monitor {

  void record(const char* event);
  void checkpoint(const char* event);
  void registerTask(const char* name, TaskHandle_t handle);
  void setSdLoggingEnabled(bool enabled);
  bool dumpToSd(const char* path = "/diagnostics/system_events.csv");
  void clear();

}  // namespace heap_monitor
