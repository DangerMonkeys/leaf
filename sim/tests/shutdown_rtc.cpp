#include <cassert>
#include <iostream>
#include "system/shutdown_guard.h"
#include "system/shutdown_rtc_policy.h"
using namespace shutdown_guard;
namespace {
  struct PrematureReset {};
  // Deterministic register timing model: a running watchdog can fire on each
  // write. SYSTEM reset leaves the RTC counter and next stage alive. This is
  // deliberately not a model of the ESP32 ROM or physical power circuitry.
  struct Rtc {
    bool protected_ = true, enabled = true, boot = true;
    uint32_t first = BACKSTOP_US, second = RESET_STAGE_US, elapsed = DEADLINE_US;
    unsigned stage = 0, systemResets = 0, rtcResets = 0;
    bool running() const { return enabled || boot; }
    void check() const {
      if (running() && elapsed >= (stage ? second : first)) throw PrematureReset{};
    }
    void advance(uint32_t us) {
      if (!running()) return;
      elapsed += us;
      if (!stage && elapsed >= first) {
        ++systemResets;
        elapsed -= first;
        stage = 1;
      }
      if (stage && elapsed >= second) {
        ++rtcResets;
        elapsed = 0;
        stage = 0;
      }
    }
  };
  struct Registers {
    Rtc* rtc;
    void unlock() { rtc->protected_ = false; }
    void lock() { rtc->protected_ = true; }
    void flashBoot(bool value) {
      assert(!rtc->protected_);
      rtc->boot = value;
      rtc->check();
    }
    void disable() {
      assert(!rtc->protected_);
      rtc->enabled = false;
      rtc->check();
    }
    void enable() {
      assert(!rtc->protected_);
      rtc->enabled = true;
      rtc->check();
    }
    void systemStage(uint32_t ticks) {
      assert(!rtc->protected_);
      rtc->first = ticks;
      rtc->check();
    }
    void rtcStage(uint32_t ticks) {
      assert(!rtc->protected_);
      rtc->second = ticks;
      rtc->check();
    }
    void feed() {
      assert(!rtc->protected_);
      rtc->elapsed = 0;
      rtc->stage = 0;
    }
  };
}  // namespace
int main() {
  // Regression: shortening the live ten-second timer after eight seconds can
  // reset before a later feed. Disabling just one enable mode is insufficient.
  for (bool ordinaryDisable : {false, true}) {
    Rtc rtc;
    Registers hw{&rtc};
    hw.unlock();
    if (ordinaryDisable) hw.disable();
    bool tripped = false;
    try {
      hw.systemStage(RESET_STAGE_US);
    } catch (const PrematureReset&) {
      tripped = true;
    }
    assert(tripped);
  }
  Rtc rtc;
  configureRtc(Registers{&rtc}, RESET_STAGE_US, RESET_STAGE_US);
  assert(rtc.protected_ && rtc.enabled && rtc.boot && rtc.elapsed == 0);
  rtc.advance(RESET_STAGE_US - 1);
  assert(rtc.systemResets == 0);
  rtc.advance(1);
  assert(rtc.systemResets == 1 && rtc.rtcResets == 0);
  rtc.advance(200000);  // A system reset must not immediately repeat at 20 ms.
  assert(rtc.rtcResets == 0);
  rtc.advance(RESET_STAGE_US - 200000);
  assert(rtc.rtcResets == 1);  // Escalate if the boot path never takes control.
  disarmRtc(Registers{&rtc});
  assert(rtc.protected_ && !rtc.enabled && !rtc.boot);
  rtc.advance(BACKSTOP_US * 2);
  assert(rtc.systemResets == 1 && rtc.rtcResets == 1);
  configureRtc(Registers{&rtc}, BACKSTOP_US, RESET_STAGE_US);
  rtc.advance(BACKSTOP_US - 1);
  assert(rtc.systemResets == 1);
  rtc.advance(1);
  assert(rtc.systemResets == 2);
  rtc.advance(RESET_STAGE_US);
  assert(rtc.rtcResets == 2);
  std::cout
      << "RTC live-timeout reconfiguration, boot allowance, escalation and full disarm passed\n";
}
