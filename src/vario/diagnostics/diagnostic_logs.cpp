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

  void printCsvString(File& file, const String& value) {
    file.print('"');
    for (size_t i = 0; i < value.length(); i++) {
      if (value[i] == '"') file.print('"');
      file.print(value[i]);
    }
    file.print('"');
  }

}  // namespace diagnostic_logs
