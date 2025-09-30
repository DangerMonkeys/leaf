#pragma once

#include <Arduino.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// This module is responsible for printing information about
// available memory periodically.  Eventually it should also
// track heap allocations by task.

/// @brief Prints memory usage to serial port
void printMemoryUsage() {
  // Heap memory info
  uint32_t freeHeap = esp_get_free_heap_size();
  uint32_t minFreeHeap = esp_get_minimum_free_heap_size();
  uint32_t usedHeap = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) - freeHeap;
  uint32_t totalHeap = freeHeap + usedHeap;

  Serial.println("=== Memory Stats ===");

  Serial.printf("Total Heap: %u KB\n", totalHeap / 1024);
  Serial.printf("Free Heap: %u KB\n", freeHeap / 1024);
  Serial.printf("Used Heap: %u KB\n", usedHeap / 1024);
  Serial.printf("Largest Free Block: %u KB\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024);
  Serial.printf("Minimum Free Heap Ever: %u KB\n", minFreeHeap / 1024);
  Serial.printf("Main Task Stack High Water Mark: %u KB\n",
                uxTaskGetStackHighWaterMark(NULL) / 1024);

#if CONFIG_SPIRAM_USE_MALLOC
  Serial.printf("Free PSRAM: %u bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  Serial.printf("Largest Free PSRAM Block: %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
#else
  Serial.println("PSRAM not available.");
#endif
  Serial.println("====================\n");
}

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
