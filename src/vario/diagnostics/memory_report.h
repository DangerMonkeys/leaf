#pragma once

#include <Arduino.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Memory report used by the dev_mode web diagnostics endpoint.
String getMemoryUsage() {
  String out;

  uint32_t freeHeap = esp_get_free_heap_size();
  uint32_t minFreeHeap = esp_get_minimum_free_heap_size();
  uint32_t usedHeap = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) - freeHeap;
  uint32_t totalHeap = freeHeap + usedHeap;

  out += "=== Memory Stats ===\n";
  out += "Total Heap: " + String(totalHeap / 1024) + " KB\n";
  out += "Free Heap: " + String(freeHeap / 1024) + " KB\n";
  out += "Used Heap: " + String(usedHeap / 1024) + " KB\n";
  out += "Largest Free Block: " + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) +
         " KB\n";
  out += "Minimum Free Heap Ever: " + String(minFreeHeap / 1024) + " KB\n";
  out += "Main Task Stack High Water Mark: " + String(uxTaskGetStackHighWaterMark(NULL) / 1024) +
         " KB\n";

#if CONFIG_SPIRAM_USE_MALLOC
  out += "Free PSRAM: " + String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) + " bytes\n";
  out +=
      "Largest Free PSRAM Block: " + String(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)) +
      " bytes\n";
#else
  out += "PSRAM not available.\n";
#endif

  out += "====================\n";
  return out;
}
