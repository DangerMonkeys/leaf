#pragma once
#include <stdint.h>

namespace shutdown_guard {
  constexpr uint32_t DEADLINE_US = 8000000;
  constexpr uint32_t HOLD_US = 10000000;
  constexpr uint32_t BACKSTOP_US = 10000000;
  constexpr unsigned LATCH_PIN = 48;
  enum class Stage : uint32_t {
    None,
    RecoveryBoot,
    Lock,
    Flight,
    BusLog,
    Settings,
    Display,
    BleWorker,
    Sound,
    Peripherals,
    Latch,
    BleDeinit,
    Charging
  };
  enum class Reason : uint32_t { None, Deadline, LongHold, WorkerTimeout, HardwareWatchdog };

  // Initialize before peripheral drivers. No task or stack is allocated.
  void init();
  bool recovering();
  void finishBoot();
  // Control API is loop-core only (the timer ISR is installed by setup()).
  bool begin(Stage stage = Stage::Lock);
  void stage(Stage value);
  void complete();
  [[noreturn]] void force(Reason reason);
  const char* stageName(Stage value);

  // Protect cleanup outside Power::shutdown too, e.g. disabling Bluetooth.
  // Nested cleanup never restarts the outer shutdown's deadline.
  class Lease {
   public:
    explicit Lease(Stage stage) : owned_(begin(stage)) {}
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    ~Lease() {
      if (owned_) complete();
    }

   private:
    bool owned_;
  };

#if defined(LEAF_SIM) || defined(LEAF_SHUTDOWN_FAULT_INJECTION)
  // Nonpersistent, one-shot faults. Hardware builds require both this explicit
  // build flag and Developer mode; ordinary firmware has no fault selector.
  void setFault(Stage value, bool watchdogOnly = false);
  Stage fault();
  void faultPoint(Stage value);
#else
  inline void faultPoint(Stage) {}
#endif
#ifdef LEAF_SIM
  struct EmergencyReset {};
  Reason lastReason();
#endif
}  // namespace shutdown_guard
