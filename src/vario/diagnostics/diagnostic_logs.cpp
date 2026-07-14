#include "diagnostics/diagnostic_logs.h"

#include <SD_MMC.h>

#include "ui/settings/settings.h"

namespace diagnostic_logs {

  bool enabled(Log log) {
    if (!settings.dev_mode) return false;

    switch (log) {
      case Log::SystemEvents:
        return settings.diag_systemEvents;
      case Log::NetworkEvents:
        return settings.diag_networkEvents;
      case Log::WebRequests:
        return settings.diag_webRequests;
      case Log::Vario:
        return settings.diag_vario;
    }
    return false;
  }

  bool ensureDirectory() {
    if (SD_MMC.exists(DIAGNOSTICS_DIR)) return true;
    return SD_MMC.mkdir(DIAGNOSTICS_DIR);
  }

  void writeSystemEventsHeaderIfNeeded(File& file, bool existed) {
    if (existed && file.size() > 0) return;
    file.println(
        "millis,source,event,detail,key,value,free_heap,min_free_heap,largest_free_block,"
        "max_alloc_heap,internal_free,internal_largest,psram_free,psram_largest,current_task,"
        "stack_high_water,loop_stack_high_water,ble_stack_high_water,"
        "fanet_tx_stack_high_water,fanet_rx_stack_high_water");
  }

  void printCsvString(File& file, const String& value) {
    file.print('"');
    for (size_t i = 0; i < value.length(); i++) {
      if (value[i] == '"') file.print('"');
      file.print(value[i]);
    }
    file.print('"');
  }

  bool appendSystemEvent(const char* source, const char* event, const String& detail,
                         const char* key, int32_t value, bool hasValue) {
    if (!enabled(Log::SystemEvents)) return false;
    if (!ensureDirectory()) return false;

    const bool existed = SD_MMC.exists(SYSTEM_EVENTS_PATH);
    File file = SD_MMC.open(SYSTEM_EVENTS_PATH, "a", true);
    if (!file) return false;

    writeSystemEventsHeaderIfNeeded(file, existed);
    file.printf("%lu,", static_cast<unsigned long>(millis()));
    printCsvString(file, source ? String(source) : String(""));
    file.print(',');
    printCsvString(file, event ? String(event) : String(""));
    file.print(',');
    printCsvString(file, detail);
    file.print(',');
    printCsvString(file, key ? String(key) : String(""));
    file.print(',');
    if (hasValue) {
      file.print(value);
    }
    file.println(",,,,,,,,,,,,,,");
    file.close();
    return true;
  }

}  // namespace diagnostic_logs
