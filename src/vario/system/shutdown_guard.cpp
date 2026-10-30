#include "system/shutdown_guard.h"
#include <Arduino.h>
#include "diagnostics/diagnostic_logs.h"
#include "hardware/configuration.h"
#include "system/shutdown_policy.h"
#include "system/shutdown_rtc_policy.h"
#include "ui/settings/settings.h"

#ifdef LEAF_SIM
#include <cstdlib>
#include "esp_system.h"
#else
#include "esp_attr.h"
#include "esp_clk_tree.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "hal/wdt_hal.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"
#endif

namespace shutdown_guard {
  namespace {
    constexpr uint32_t MAGIC = 0x53484f46;
    struct Record {
      uint32_t magic, stage, reason;
    };
#ifdef LEAF_SIM
    volatile Record retained{};
#else
    RTC_NOINIT_ATTR volatile Record retained;
    wdt_hal_context_t rtcWatchdog = RWDT_HAL_CONTEXT_DEFAULT();
    uint32_t backstopTicks = 0, resetTicks = 0;
#endif
    // All control calls run on Arduino's loop core; tick is that core's ISR.
    // Aligned 32-bit volatile accesses are single Xtensa instructions. Avoid
    // std::atomic RMW here: this toolchain implements them with spinlocks.
    volatile uint32_t startedUs = 0, active = 0, forced = 0, suppressHold = 0;
    HoldPolicy hold;
    bool initialized = false, recovery = false;
    Stage recoveredStage = Stage::None;
    Reason recoveredReason = Reason::None;
    hw_timer_t* timer = nullptr;
#if defined(LEAF_SIM) || defined(LEAF_SHUTDOWN_FAULT_INJECTION)
    Stage injected = Stage::None;
    FaultBehavior faultBehavior = FaultBehavior::Stall;
    volatile bool skipGuard = false;
#endif
#ifdef LEAF_SIM
    Reason forcedReason = Reason::None;
    uint32_t simRtcArmedUs = 0;
    void armRtc() { simRtcArmedUs = micros(); }
    void stopRtc() {}
#else
    // Every adapter operation is forced inline into the IRAM emergency path.
    struct RtcHardware {
      rtc_cntl_dev_t* hw;
      __attribute__((always_inline)) inline void unlock() { rwdt_ll_write_protect_disable(hw); }
      __attribute__((always_inline)) inline void lock() { rwdt_ll_write_protect_enable(hw); }
      __attribute__((always_inline)) inline void flashBoot(bool enabled) {
        rwdt_ll_set_flashboot_en(hw, enabled);
      }
      __attribute__((always_inline)) inline void disable() { rwdt_ll_disable(hw); }
      __attribute__((always_inline)) inline void enable() { rwdt_ll_enable(hw); }
      __attribute__((always_inline)) inline void feed() { rwdt_ll_feed(hw); }
      __attribute__((always_inline)) inline void systemStage(uint32_t ticks) {
        rwdt_ll_config_stage(hw, WDT_STAGE0, ticks, WDT_STAGE_ACTION_RESET_SYSTEM);
      }
      __attribute__((always_inline)) inline void rtcStage(uint32_t ticks) {
        rwdt_ll_config_stage(hw, WDT_STAGE1, ticks, WDT_STAGE_ACTION_RESET_RTC);
      }
    };
    void armRtc() { configureRtc(RtcHardware{RWDT_DEV_GET()}, backstopTicks, resetTicks); }
    void stopRtc() { disarmRtc(RtcHardware{RWDT_DEV_GET()}); }
#endif
#ifndef LEAF_SIM
    void IRAM_ATTR hardwareTrip() {
      gpio_ll_set_level(&GPIO, LATCH_PIN, 0);
      gpio_ll_iomux_func_sel(IO_MUX_GPIO48_REG, PIN_FUNC_GPIO);
      gpio_ll_output_enable(&GPIO, LATCH_PIN);
      // State 1 is published only after armRtc() finishes. Preserve that
      // original backstop: native RTCB recovers from the late shutdown state
      // with this timer intact, whereas the CLR emergency path failed.
      ensureEmergencyRtc(RtcHardware{RWDT_DEV_GET()}, active == 1, resetTicks, resetTicks);
    }
#endif
    // This emergency path has no mutex, heap, logging, drivers, or software
    // restart handlers. GPIO works even with a wedged I2C bus. RTC reset is
    // independent of the interrupted task and of the digital timer interrupt.
    void IRAM_ATTR trip(Reason why) {
      if (forced) return;
      forced = 1;
      if (!active) retained.stage = uint32_t(Stage::None);
      retained.reason = uint32_t(why);
      retained.magic = MAGIC;
      asm volatile("" ::: "memory");
#ifdef LEAF_SIM
      forcedReason = why;
      digitalWrite(LATCH_PIN, LOW);
      char marker[40];
      snprintf(marker, sizeof(marker), "%lu:%lu", (unsigned long)retained.stage,
               (unsigned long)retained.reason);
      setenv("LEAF_SIM_SHUTDOWN_RECOVERY", marker, 1);
      esp_restart();
      throw EmergencyReset{};
#else
      hardwareTrip();
#endif
    }
    void IRAM_ATTR tick() {
#if defined(LEAF_SIM) || defined(LEAF_SHUTDOWN_FAULT_INJECTION)
      if (skipGuard) return;
#endif
#ifdef LEAF_SIM
      const uint32_t now = micros();
      const bool pressed = digitalRead(BUTTON_PIN_CENTER);
#else
      const uint32_t now = uint32_t(esp_timer_get_time());
      const bool pressed = gpio_ll_get_level(&GPIO, BUTTON_PIN_CENTER);
#endif
      if (suppressHold) {
        suppressHold = 0;
        hold.held = false;
        hold.requireRelease = true;
      }
      if (hold.update(now, pressed)) trip(Reason::LongHold);
      if (active == 1 && expired(now, startedUs)) trip(Reason::Deadline);
    }
#ifdef LEAF_SIM
    void backstop() {
      if (active && expired(micros(), simRtcArmedUs, BACKSTOP_US)) trip(Reason::HardwareWatchdog);
    }
#endif
  }  // namespace

  const char* stageName(Stage value) {
    switch (value) {
      case Stage::None:
        return "none";
      case Stage::RecoveryBoot:
        return "recovery-boot";
      case Stage::Lock:
        return "logbook-lock";
      case Stage::Flight:
        return "flight-save";
      case Stage::BusLog:
        return "bus-log";
      case Stage::Settings:
        return "settings-save";
      case Stage::Display:
        return "goodbye-display";
      case Stage::BleWorker:
        return "ble-worker";
      case Stage::Sound:
        return "shutdown-sound";
      case Stage::Peripherals:
        return "peripherals";
      case Stage::Latch:
        return "power-latch";
      case Stage::BleDeinit:
        return "ble-deinit";
      case Stage::Charging:
        return "charging";
      case Stage::Sleeping:
        return "peripherals-asleep";
      case Stage::LatchReleased:
        return "latch-released";
    }
    return "unknown";
  }
  void init() {
    if (initialized) return;
    initialized = true;
#ifdef LEAF_SIM
    const char* marker = std::getenv("LEAF_SIM_SHUTDOWN_RECOVERY");
    recovery = marker != nullptr;
    if (marker) {
      char* next = nullptr;
      retained.stage = std::strtoul(marker, &next, 10);
      retained.reason = *next == ':' ? std::strtoul(next + 1, nullptr, 10) : 0;
    }
    unsetenv("LEAF_SIM_SHUTDOWN_RECOVERY");
#else
    const auto reset = esp_reset_reason();
    recovery = reset != ESP_RST_POWERON && reset != ESP_RST_BROWNOUT &&
               reset != ESP_RST_DEEPSLEEP && retained.magic == MAGIC;
#endif
    if (recovery) {
      recoveredStage = Stage(retained.stage);
      recoveredReason = Reason(retained.reason);
      pinMode(LATCH_PIN, OUTPUT);
      digitalWrite(LATCH_PIN, LOW);
      hold.requireRelease = true;
    }
    retained.magic = 0;
    pinMode(BUTTON_PIN_CENTER, INPUT);
#ifndef LEAF_SIM
    uint32_t slowHz = 0;
    ESP_ERROR_CHECK(esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_RTC_SLOW,
                                                 ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &slowHz));
    // LL stage-0 uses half the effective tick count; round up to an even count.
    backstopTicks = (uint32_t(uint64_t(slowHz) * BACKSTOP_US / 1000000) + 1) & ~1u;
    resetTicks = (uint32_t(uint64_t(slowHz) * RESET_STAGE_US / 1000000) + 1) & ~1u;
    wdt_hal_init(&rtcWatchdog, WDT_RWDT, 0, false);
    stopRtc();
#endif
    timer = timerBegin(1000000);
    if (!timer) {
      // Fail closed: a build that cannot install its guard must not claim
      // normal operation. Keep power released and reset via the RTC backstop.
      begin(Stage::RecoveryBoot);
      force(Reason::HardwareWatchdog);
    }
    timerAttachInterrupt(timer, tick);
    timerAlarm(timer, 10000, true, 0);
#ifdef LEAF_SIM
    auto* rtc = timerBegin(1000000);
    timerAttachInterrupt(rtc, backstop);
    timerAlarm(rtc, 10000, true, 0);
#endif
    if (recovery) begin(Stage::RecoveryBoot);
  }
  bool recovering() { return recovery; }
  bool begin(Stage value) {
    // State 2 reserves initialization; the ISR must not inspect an old start
    // timestamp while a new deadline is being armed.
    if (active) {
      stage(value);
      return false;
    }
    active = 2;
    retained.stage = uint32_t(value);
    retained.reason = uint32_t(Reason::HardwareWatchdog);
    retained.magic = MAGIC;
    startedUs = micros();
    armRtc();
    if (forced) force(Reason::Deadline);
    asm volatile("" ::: "memory");
    active = 1;
    return true;
  }
  void stage(Stage value) { retained.stage = uint32_t(value); }
  void complete() {
    if (forced) force(Reason::Deadline);
    stopRtc();
    active = 0;
    retained.magic = 0;
    suppressHold = 1;
    // If an emergency interrupt preempted completion, restore its reset
    // marker/backstop rather than clearing a shutdown that already tripped.
    if (forced) {
      retained.magic = MAGIC;
      force(Reason::Deadline);
    }
  }
  void finishBoot() {
    if (!recovery) return;
    Serial.printf("Recovered shutdown: stage=%s reason=%lu\n", stageName(recoveredStage),
                  (unsigned long)recoveredReason);
    diagnostic_logs::appendSystemEvent("shutdown", "recovered", stageName(recoveredStage), "reason",
                                       int32_t(recoveredReason), true);
    complete();
  }
  [[noreturn]] void force(Reason why) {
    trip(why);
#ifndef LEAF_SIM
    // If an ISR preempted begin()/complete(), their resumed writes may have
    // replaced/disarmed its reset timer. States 2/0 need a new timer; state 1
    // keeps the original backstop. Never wait on the interrupted code.
    hardwareTrip();
#endif
    for (;;) { /* Hardware reset needs no scheduler, flash, or interrupts. */
    }
  }
#if defined(LEAF_SIM) || defined(LEAF_SHUTDOWN_FAULT_INJECTION)
  void setFault(Stage value, FaultBehavior behavior) {
    injected = value;
    faultBehavior = behavior;
  }
  Stage fault() { return injected; }
  void faultPoint(Stage value) {
    stage(value);
    if (injected != value || !settings.dev_mode) return;
    injected = Stage::None;
    Serial.printf("Injecting shutdown stall: %s\n", stageName(value));
    if (faultBehavior == FaultBehavior::RtcOnly) skipGuard = true;
    if (faultBehavior == FaultBehavior::InterruptsOff) {
#ifdef LEAF_SIM
      skipGuard = true;
#else
      noInterrupts();  // Existing interrupt WDT may reset before the RTC backstop.
      for (;;) asm volatile("nop");
#endif
    }
    for (;;) delay(10);
  }
#endif
#ifdef LEAF_SIM
  Reason lastReason() { return forcedReason; }
#endif
}  // namespace shutdown_guard
