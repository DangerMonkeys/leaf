#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace heap_monitor {

  struct Snapshot {
    uint32_t internalFree;
    uint32_t internalMin;
    uint32_t internalLargest;
    uint32_t loopStack;
    uint32_t bleStack;
    uint32_t fanetTxStack;
    uint32_t fanetRxStack;
  };
  void snapshot(Snapshot& sample);

  void record(const char* event);
  void checkpoint(const char* event);
  void registerTask(const char* name, TaskHandle_t handle);
  void setSdLoggingEnabled(bool enabled);
  bool dumpToSd(const char* path = "/diagnostics/system_events.csv");
  void clear();

}  // namespace heap_monitor
