#include "diagnostics/cpu_utilization.h"

#include <SD_MMC.h>
#include <string.h>

#include "diagnostics/diagnostic_logs.h"

namespace cpu_utilization {
  namespace {
    constexpr uint8_t BLOCKS_PER_SECOND = 100;
    constexpr uint8_t LAST_BLOCK_INDEX = BLOCKS_PER_SECOND - 1;

    struct Report {
      uint32_t millis;
      uint32_t sequence;
      uint32_t totalUs;
      uint32_t maxUs;
      uint16_t overrunCount;
      uint16_t droppedReports;
      uint32_t previousWriteUs;
      uint32_t blocksUs[BLOCKS_PER_SECOND];
      uint8_t buttonMasks[BLOCKS_PER_SECOND];
      uint8_t buttonEventMasks[BLOCKS_PER_SECOND];
      uint8_t displayContexts[BLOCKS_PER_SECOND];
    };

    Report active = {};
    Report pending = {};
    bool pendingReady = false;
    uint32_t sequence = 0;
    uint16_t droppedReports = 0;
    uint32_t lastWriteUs = 0;
    uint8_t currentButtonEventMask = 0;

    void resetActive() {
      memset(&active, 0, sizeof(active));
      active.millis = millis();
      active.sequence = ++sequence;
      active.droppedReports = droppedReports;
      active.previousWriteUs = lastWriteUs;
    }

    void writeHeaderIfNeeded(File& file, bool existed) {
      if (existed && file.size() > 0) return;

      file.print(
          "millis,sequence,total_us,max_us,avg_us,overrun_count,dropped_reports,previous_write_us");
      for (uint8_t i = 0; i < BLOCKS_PER_SECOND; i++) {
        file.printf(",b%02u_us", i);
      }
      for (uint8_t i = 0; i < BLOCKS_PER_SECOND; i++) {
        file.printf(",b%02u_button_mask", i);
      }
      for (uint8_t i = 0; i < BLOCKS_PER_SECOND; i++) {
        file.printf(",b%02u_button_event_mask", i);
      }
      for (uint8_t i = 0; i < BLOCKS_PER_SECOND; i++) {
        file.printf(",b%02u_display_context", i);
      }
      file.println();
    }

    void writeReport(File& file, const Report& report) {
      const uint32_t avgUs = report.totalUs / BLOCKS_PER_SECOND;
      file.printf("%lu,%lu,%lu,%lu,%lu,%u,%u,%lu", static_cast<unsigned long>(report.millis),
                  static_cast<unsigned long>(report.sequence),
                  static_cast<unsigned long>(report.totalUs),
                  static_cast<unsigned long>(report.maxUs), static_cast<unsigned long>(avgUs),
                  report.overrunCount, report.droppedReports,
                  static_cast<unsigned long>(report.previousWriteUs));
      for (uint8_t i = 0; i < BLOCKS_PER_SECOND; i++) {
        file.printf(",%lu", static_cast<unsigned long>(report.blocksUs[i]));
      }
      for (uint8_t i = 0; i < BLOCKS_PER_SECOND; i++) {
        file.printf(",%u", report.buttonMasks[i]);
      }
      for (uint8_t i = 0; i < BLOCKS_PER_SECOND; i++) {
        file.printf(",%u", report.buttonEventMasks[i]);
      }
      for (uint8_t i = 0; i < BLOCKS_PER_SECOND; i++) {
        file.printf(",%u", report.displayContexts[i]);
      }
      file.println();
    }

    void finalizeActive() {
      if (pendingReady) droppedReports++;
      pending = active;
      pendingReady = true;
      resetActive();
    }
  }  // namespace

  bool enabled() { return diagnostic_logs::enabled(diagnostic_logs::Log::CpuUtilization); }

  uint8_t buttonPinMask(Button button) {
    switch (button) {
      case Button::UP:
        return 1u << 0;
      case Button::DOWN:
        return 1u << 1;
      case Button::LEFT:
        return 1u << 2;
      case Button::RIGHT:
        return 1u << 3;
      case Button::CENTER:
        return 1u << 4;
      case Button::NONE:
        return 0;
    }
    return 0;
  }

  void recordButtonEvent(Button button, ButtonEvent event) {
    if (!enabled()) return;

    uint8_t mask = buttonPinMask(button);
    if (mask == 0) mask = 1u << 5;
    currentButtonEventMask |= mask;
  }

  void recordBlock(uint8_t blockIndex, uint32_t startUs, uint32_t endUs, uint8_t buttonMask,
                   uint8_t displayContext) {
    if (!enabled()) return;

    if (active.sequence == 0) resetActive();
    if (blockIndex >= BLOCKS_PER_SECOND) return;

    const uint32_t elapsedUs = endUs - startUs;
    active.blocksUs[blockIndex] = elapsedUs;
    active.buttonMasks[blockIndex] = buttonMask;
    active.buttonEventMasks[blockIndex] = currentButtonEventMask;
    active.displayContexts[blockIndex] = displayContext;
    currentButtonEventMask = 0;
    active.totalUs += elapsedUs;
    if (elapsedUs > active.maxUs) active.maxUs = elapsedUs;
    if (elapsedUs > 10000) active.overrunCount++;

    if (blockIndex == LAST_BLOCK_INDEX) finalizeActive();
  }

  void writePendingReport() {
    if (!enabled()) return;
    if (!pendingReady) return;
    if (!diagnostic_logs::ensureDirectory()) return;

    const uint32_t writeStartUs = micros();
    const bool existed = SD_MMC.exists(diagnostic_logs::CPU_UTILIZATION_PATH);
    File file = SD_MMC.open(diagnostic_logs::CPU_UTILIZATION_PATH, "a", true);
    if (!file) return;

    writeHeaderIfNeeded(file, existed);
    writeReport(file, pending);
    file.close();
    lastWriteUs = micros() - writeStartUs;
    pendingReady = false;
  }

}  // namespace cpu_utilization
