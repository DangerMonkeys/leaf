#pragma once
#include <stdint.h>

namespace task_timer {
  constexpr uint32_t FREQUENCY_HZ = 1000000;
  constexpr uint32_t PERIOD_US = 10000;
  constexpr uint32_t OPTIMIZER_BUDGET_US = 2000;

  // The counter auto-reloads independently of delivery of its interrupt. Include
  // time spent reading it, and one counter quantum, before admitting work.
  inline bool hasHeadroom(uint64_t phaseUs, uint64_t readElapsedUs) {
    return phaseUs < PERIOD_US && readElapsedUs < PERIOD_US &&
           phaseUs + readElapsedUs + OPTIMIZER_BUDGET_US + 1 < PERIOD_US;
  }

  class CaptureReference {
   public:
    void reset(uint64_t fallbackEpochUs) {
      epochUs_ = fallbackEpochUs;
      anchored_ = false;
    }
    uint64_t expected(uint32_t tick, uint64_t readStartUs, uint64_t phaseUs) {
      if (!anchored_ && phaseUs < PERIOD_US) {
        epochUs_ = readStartUs - phaseUs - uint64_t(tick) * PERIOD_US;
        anchored_ = true;
      }
      // Anchor only once per capture. Interrupt loss DURING a capture remains
      // visible as a growing offset; never re-anchor at every dispatch.
      return epochUs_ + uint64_t(tick) * PERIOD_US;
    }

   private:
    uint64_t epochUs_ = 0;
    bool anchored_ = false;
  };
}  // namespace task_timer
