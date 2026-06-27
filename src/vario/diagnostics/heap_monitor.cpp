#include "heap_monitor.h"

#include <Arduino.h>
#include <SD_MMC.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace heap_monitor {
namespace {
constexpr size_t SAMPLE_COUNT = 32;
constexpr size_t EVENT_LENGTH = 28;
constexpr const char* DIAGNOSTICS_DIR = "/diagnostics";

struct Sample {
  uint32_t millis;
  char event[EVENT_LENGTH];
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  uint32_t largestFreeBlock;
  uint32_t maxAllocHeap;
  uint32_t stackHighWater;
};

Sample samples[SAMPLE_COUNT];
size_t nextSample = 0;
size_t sampleTotal = 0;

void copyEvent(char* dest, const char* event) {
  if (!event) event = "";
  size_t i = 0;
  for (; i + 1 < EVENT_LENGTH && event[i] != '\0'; i++) {
    dest[i] = event[i];
  }
  dest[i] = '\0';
}

bool ensureDiagnosticsDirectory() {
  if (SD_MMC.exists(DIAGNOSTICS_DIR)) return true;
  return SD_MMC.mkdir(DIAGNOSTICS_DIR);
}

void writeHeaderIfNeeded(File& file, const char* path) {
  if (SD_MMC.exists(path) && file.size() > 0) return;
  file.println("millis,event,free_heap,min_free_heap,largest_free_block,max_alloc_heap,"
               "stack_high_water");
}

}  // namespace

void record(const char* event) {
  Sample& sample = samples[nextSample];
  sample.millis = millis();
  copyEvent(sample.event, event);
  sample.freeHeap = esp_get_free_heap_size();
  sample.minFreeHeap = esp_get_minimum_free_heap_size();
  sample.largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  sample.maxAllocHeap = ESP.getMaxAllocHeap();
  sample.stackHighWater = uxTaskGetStackHighWaterMark(NULL);

  nextSample = (nextSample + 1) % SAMPLE_COUNT;
  if (sampleTotal < SAMPLE_COUNT) sampleTotal++;
}

bool dumpToSd(const char* path) {
  if (!path || path[0] == '\0') return false;
  if (!ensureDiagnosticsDirectory()) return false;

  const bool existed = SD_MMC.exists(path);
  File file = SD_MMC.open(path, "a", true);
  if (!file) return false;

  if (!existed || file.size() == 0) writeHeaderIfNeeded(file, path);

  const size_t start = sampleTotal < SAMPLE_COUNT ? 0 : nextSample;
  for (size_t i = 0; i < sampleTotal; i++) {
    const Sample& sample = samples[(start + i) % SAMPLE_COUNT];
    file.printf("%lu,%s,%lu,%lu,%lu,%lu,%lu\n", static_cast<unsigned long>(sample.millis),
                sample.event, static_cast<unsigned long>(sample.freeHeap),
                static_cast<unsigned long>(sample.minFreeHeap),
                static_cast<unsigned long>(sample.largestFreeBlock),
                static_cast<unsigned long>(sample.maxAllocHeap),
                static_cast<unsigned long>(sample.stackHighWater));
  }
  file.close();
  return true;
}

void clear() {
  nextSample = 0;
  sampleTotal = 0;
}

}  // namespace heap_monitor
