#include "heap_monitor.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "diagnostics/diagnostic_logs.h"
#include "ui/settings/settings.h"

namespace heap_monitor {
  namespace {
    constexpr size_t SAMPLE_COUNT = 32;
    constexpr size_t EVENT_LENGTH = 28;
    constexpr size_t TASK_NAME_LENGTH = 12;
    constexpr size_t REGISTERED_TASK_COUNT = 4;
    struct Sample {
      uint32_t millis;
      char event[EVENT_LENGTH];
      uint32_t freeHeap;
      uint32_t minFreeHeap;
      uint32_t largestFreeBlock;
      uint32_t maxAllocHeap;
      uint32_t internalFree;
      uint32_t internalLargest;
      uint32_t psramFree;
      uint32_t psramLargest;
      uint32_t stackHighWater;
      uint32_t loopStackHighWater;
      uint32_t bleStackHighWater;
      uint32_t fanetTxStackHighWater;
      uint32_t fanetRxStackHighWater;
      char currentTask[TASK_NAME_LENGTH];
    };

    struct RegisteredTask {
      char name[TASK_NAME_LENGTH];
      TaskHandle_t handle;
    };

    Sample samples[SAMPLE_COUNT];
    size_t nextSample = 0;
    size_t sampleTotal = 0;
    RegisteredTask registeredTasks[REGISTERED_TASK_COUNT] = {};
    bool sdLoggingEnabled = false;

    void copyEvent(char* dest, const char* event) {
      if (!event) event = "";
      size_t i = 0;
      for (; i + 1 < EVENT_LENGTH && event[i] != '\0'; i++) {
        dest[i] = event[i];
      }
      dest[i] = '\0';
    }

    void copyTaskName(char* dest, const char* name) {
      if (!name) name = "";
      size_t i = 0;
      for (; i + 1 < TASK_NAME_LENGTH && name[i] != '\0'; i++) {
        dest[i] = name[i];
      }
      dest[i] = '\0';
    }

    uint32_t stackHighWaterFor(const char* name) {
      for (const RegisteredTask& task : registeredTasks) {
        if (task.handle && strncmp(task.name, name, TASK_NAME_LENGTH) == 0) {
          return uxTaskGetStackHighWaterMark(task.handle);
        }
      }
      return 0;
    }

    void writeHeaderIfNeeded(File& file, const char* path) {
      if (SD_MMC.exists(path) && file.size() > 0) return;
      file.println(
          "millis,source,event,detail,key,value,free_heap,min_free_heap,largest_free_block,max_alloc_heap,"
          "internal_free,internal_largest,psram_free,psram_largest,current_task,"
          "stack_high_water,loop_stack_high_water,ble_stack_high_water,"
          "fanet_tx_stack_high_water,fanet_rx_stack_high_water");
    }

    void captureSample(Sample& sample, const char* event) {
      sample.millis = millis();
      copyEvent(sample.event, event);
      sample.freeHeap = esp_get_free_heap_size();
      sample.minFreeHeap = esp_get_minimum_free_heap_size();
      sample.largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      sample.maxAllocHeap = ESP.getMaxAllocHeap();
      sample.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      sample.internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
      sample.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      sample.psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
      sample.stackHighWater = uxTaskGetStackHighWaterMark(NULL);
      sample.loopStackHighWater = stackHighWaterFor("loop");
      sample.bleStackHighWater = stackHighWaterFor("ble");
      sample.fanetTxStackHighWater = stackHighWaterFor("fanet_tx");
      sample.fanetRxStackHighWater = stackHighWaterFor("fanet_rx");
      copyTaskName(sample.currentTask, pcTaskGetName(NULL));
    }

    void writeSample(File& file, const char* source, const Sample& sample) {
      file.printf("%lu,%s,%s,,,,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%s,%lu,%lu,%lu,%lu,%lu\n",
                  static_cast<unsigned long>(sample.millis), source ? source : "heap",
                  sample.event,
                  static_cast<unsigned long>(sample.freeHeap),
                  static_cast<unsigned long>(sample.minFreeHeap),
                  static_cast<unsigned long>(sample.largestFreeBlock),
                  static_cast<unsigned long>(sample.maxAllocHeap),
                  static_cast<unsigned long>(sample.internalFree),
                  static_cast<unsigned long>(sample.internalLargest),
                  static_cast<unsigned long>(sample.psramFree),
                  static_cast<unsigned long>(sample.psramLargest), sample.currentTask,
                  static_cast<unsigned long>(sample.stackHighWater),
                  static_cast<unsigned long>(sample.loopStackHighWater),
                  static_cast<unsigned long>(sample.bleStackHighWater),
                  static_cast<unsigned long>(sample.fanetTxStackHighWater),
                  static_cast<unsigned long>(sample.fanetRxStackHighWater));
    }

  }  // namespace

  void record(const char* event) {
    if (!diagnostic_logs::enabled(diagnostic_logs::Log::SystemEvents)) return;
    Sample& sample = samples[nextSample];
    captureSample(sample, event);

    nextSample = (nextSample + 1) % SAMPLE_COUNT;
    if (sampleTotal < SAMPLE_COUNT) sampleTotal++;
  }

  void checkpoint(const char* event) {
    if (!diagnostic_logs::enabled(diagnostic_logs::Log::SystemEvents)) return;

    Sample sample;
    captureSample(sample, event);

    samples[nextSample] = sample;
    nextSample = (nextSample + 1) % SAMPLE_COUNT;
    if (sampleTotal < SAMPLE_COUNT) sampleTotal++;

    if (!sdLoggingEnabled || !diagnostic_logs::ensureDirectory()) return;

    const bool existed = SD_MMC.exists(diagnostic_logs::SYSTEM_EVENTS_PATH);
    File file = SD_MMC.open(diagnostic_logs::SYSTEM_EVENTS_PATH, "a", true);
    if (!file) return;
    if (!existed || file.size() == 0) writeHeaderIfNeeded(file, diagnostic_logs::SYSTEM_EVENTS_PATH);
    writeSample(file, "heap", sample);
    file.close();
  }

  void registerTask(const char* name, TaskHandle_t handle) {
    if (!name) return;

    for (RegisteredTask& task : registeredTasks) {
      if (strncmp(task.name, name, TASK_NAME_LENGTH) == 0) {
        task.handle = handle;
        return;
      }
    }

    if (!handle) return;

    for (RegisteredTask& task : registeredTasks) {
      if (!task.handle) {
        copyTaskName(task.name, name);
        task.handle = handle;
        return;
      }
    }
  }

  void setSdLoggingEnabled(bool enabled) { sdLoggingEnabled = enabled; }

  bool dumpToSd(const char* path) {
    if (!diagnostic_logs::enabled(diagnostic_logs::Log::SystemEvents)) return false;
    if (!path || path[0] == '\0') return false;
    if (!sdLoggingEnabled) return false;
    if (!diagnostic_logs::ensureDirectory()) return false;

    const char* outputPath = diagnostic_logs::SYSTEM_EVENTS_PATH;
    const bool existed = SD_MMC.exists(outputPath);
    File file = SD_MMC.open(outputPath, "a", true);
    if (!file) return false;

    if (!existed || file.size() == 0) writeHeaderIfNeeded(file, outputPath);

    const size_t start = sampleTotal < SAMPLE_COUNT ? 0 : nextSample;
    for (size_t i = 0; i < sampleTotal; i++) {
      const Sample& sample = samples[(start + i) % SAMPLE_COUNT];
      writeSample(file, "heap_snapshot", sample);
    }
    file.close();
    return true;
  }

  void clear() {
    if (!diagnostic_logs::enabled(diagnostic_logs::Log::SystemEvents)) return;
    nextSample = 0;
    sampleTotal = 0;
  }

}  // namespace heap_monitor
