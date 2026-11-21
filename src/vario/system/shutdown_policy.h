#pragma once
#include <stdint.h>
#include "system/shutdown_guard.h"

namespace shutdown_guard {
  // ISR-owned button state. Unsigned elapsed arithmetic tolerates micros wrap.
  struct HoldPolicy {
    uint32_t heldSince = 0;
    bool held = false;
    bool requireRelease = false;
    __attribute__((always_inline)) inline bool update(uint32_t now, bool pressed) {
      if (!pressed) {
        held = false;
        requireRelease = false;
        return false;
      }
      if (requireRelease) return false;
      if (!held) {
        held = true;
        heldSince = now;
      }
      return uint32_t(now - heldSince) >= HOLD_US;
    }
  };
  __attribute__((always_inline)) inline bool expired(uint32_t now, uint32_t start,
                                                     uint32_t limit = DEADLINE_US) {
    return uint32_t(now - start) >= limit;
  }
}  // namespace shutdown_guard
