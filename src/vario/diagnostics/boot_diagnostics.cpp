#include "diagnostics/boot_diagnostics.h"

#include <Arduino.h>
#include <SD_MMC.h>

#include "diagnostics/heap_monitor.h"
#include "esp_system.h"
#include "navigation/gpx.h"
#include "ui/settings/settings.h"

namespace boot_diagnostics {
  namespace {
    constexpr const char* DIAGNOSTICS_DIR = "/diagnostics";
    constexpr const char* BOOT_REPORT_PATH = "/diagnostics/boot_report.csv";

    esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;
    bool resetReasonCaptured = false;
    bool reportsWritten = false;

    const char* resetReasonName(esp_reset_reason_t reason) {
      switch (reason) {
        case ESP_RST_POWERON:
          return "poweron";
        case ESP_RST_EXT:
          return "external";
        case ESP_RST_SW:
          return "software";
        case ESP_RST_PANIC:
          return "panic";
        case ESP_RST_INT_WDT:
          return "interrupt_watchdog";
        case ESP_RST_TASK_WDT:
          return "task_watchdog";
        case ESP_RST_WDT:
          return "other_watchdog";
        case ESP_RST_DEEPSLEEP:
          return "deepsleep";
        case ESP_RST_BROWNOUT:
          return "brownout";
        case ESP_RST_SDIO:
          return "sdio";
        case ESP_RST_USB:
          return "usb";
        case ESP_RST_JTAG:
          return "jtag";
        case ESP_RST_EFUSE:
          return "efuse";
        case ESP_RST_PWR_GLITCH:
          return "power_glitch";
        case ESP_RST_CPU_LOCKUP:
          return "cpu_lockup";
        case ESP_RST_UNKNOWN:
        default:
          return "unknown";
      }
    }

    bool ensureDiagnosticsDirectory() {
      if (SD_MMC.exists(DIAGNOSTICS_DIR)) return true;
      return SD_MMC.mkdir(DIAGNOSTICS_DIR);
    }

    void writeHeaderIfNeeded(File& file, bool existed) {
      if (existed && file.size() > 0) return;
      file.println("millis,section,key,value,detail");
    }

    void writeRow(File& file, const char* section, const char* key, size_t value,
                  const char* detail) {
      file.printf("%lu,%s,%s,%lu,%s\n", static_cast<unsigned long>(millis()), section, key,
                  static_cast<unsigned long>(value), detail ? detail : "");
    }

    void writeNavSizeReport(File& file) {
      writeRow(file, "nav_size", "sizeof_waypoint", sizeof(Waypoint), "bytes");
      writeRow(file, "nav_size", "sizeof_route_point", sizeof(RoutePoint), "bytes");
      writeRow(file, "nav_size", "sizeof_route", sizeof(Route), "bytes");
      writeRow(file, "nav_size", "sizeof_navigator", sizeof(Navigator), "bytes");
      writeRow(file, "nav_size", "waypoint_capacity", maxNavPoints + 1, "entries");
      writeRow(file, "nav_size", "route_point_capacity", maxRoutePointRefs + 1, "entries");
      writeRow(file, "nav_size", "route_capacity", maxRoutes + 1, "entries");
      writeRow(file, "nav_size", "waypoints_array_bytes", sizeof(Waypoint) * (maxNavPoints + 1),
               "bytes");
      writeRow(file, "nav_size", "route_points_array_bytes",
               sizeof(RoutePoint) * (maxRoutePointRefs + 1), "bytes");
      writeRow(file, "nav_size", "routes_array_bytes", sizeof(Route) * (maxRoutes + 1), "bytes");
    }

    void printNavSizeReport() {
      Serial.printf("Nav memory: Waypoint=%u RoutePoint=%u Route=%u Navigator=%u bytes\n",
                    static_cast<unsigned int>(sizeof(Waypoint)),
                    static_cast<unsigned int>(sizeof(RoutePoint)),
                    static_cast<unsigned int>(sizeof(Route)),
                    static_cast<unsigned int>(sizeof(Navigator)));
      Serial.printf("Nav arrays: waypoints=%u routePoints=%u routes=%u bytes\n",
                    static_cast<unsigned int>(sizeof(Waypoint) * (maxNavPoints + 1)),
                    static_cast<unsigned int>(sizeof(RoutePoint) * (maxRoutePointRefs + 1)),
                    static_cast<unsigned int>(sizeof(Route) * (maxRoutes + 1)));
    }

  }  // namespace

  void captureResetReason() {
    resetReason = esp_reset_reason();
    resetReasonCaptured = true;
    reportsWritten = false;

    Serial.printf("Reset reason: %s (%d)\n", resetReasonName(resetReason),
                  static_cast<int>(resetReason));
  }

  void writeReportsToSd() {
    if (!settings.dev_mode || reportsWritten || !ensureDiagnosticsDirectory()) return;

    const bool existed = SD_MMC.exists(BOOT_REPORT_PATH);
    File file = SD_MMC.open(BOOT_REPORT_PATH, "a", true);
    if (!file) return;

    writeHeaderIfNeeded(file, existed);
    if (!resetReasonCaptured) captureResetReason();
    writeRow(file, "reset", "reason", static_cast<size_t>(resetReason),
             resetReasonName(resetReason));
    writeNavSizeReport(file);
    file.close();

    printNavSizeReport();
    heap_monitor::checkpoint("boot-reset");
    heap_monitor::checkpoint("nav-size-report");
    reportsWritten = true;
  }

}  // namespace boot_diagnostics
