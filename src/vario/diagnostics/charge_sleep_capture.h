#pragma once

#include <cstdint>

namespace charge_sleep_capture {
  enum class State { Idle, Armed, Recording, Ready };

  enum Blocker : uint8_t {
    ButtonHeld = 1 << 0,
    DiagnosticNetwork = 1 << 1,
    LeafLog = 1 << 2,
    UsbHost = 1 << 3,
  };

  struct Sleep {
    uint32_t requestedUs = 0;
    uint32_t callUs = 0;
    int timerResult = -1;
    int result = -1;
    int wakeCause = -1;
  };

  State state();
  bool arm();
  void finish();
  void recordTick(uint32_t tickUs, uint8_t blockers);
  void recordButtonBlock();
  void recordSleep(const Sleep& sleep);
  bool exportToSd();
}  // namespace charge_sleep_capture
