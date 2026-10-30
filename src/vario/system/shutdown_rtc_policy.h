#pragma once
#include <stdint.h>

namespace shutdown_guard {
  // Shared by the native register adapter and its host timing model. Both the
  // normal enable and flash-boot mode must be off before shortening an armed
  // timeout: otherwise the old elapsed count can reset the chip before feed().
  template <class Hardware>
  __attribute__((always_inline)) inline void configureRtc(Hardware hw, uint32_t firstTicks,
                                                          uint32_t secondTicks) {
    hw.unlock();
    hw.flashBoot(false);
    hw.disable();
    hw.systemStage(firstTicks);
    hw.rtcStage(secondTicks);
    hw.feed();
    hw.flashBoot(true);
    hw.enable();
    hw.lock();
  }
  template <class Hardware>
  __attribute__((always_inline)) inline void disarmRtc(Hardware hw) {
    hw.unlock();
    hw.flashBoot(false);
    hw.disable();
    hw.lock();
  }
}  // namespace shutdown_guard
