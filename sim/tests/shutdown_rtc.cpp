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
    unsigned stage = 0, systemResets = 0, rtcResets = 0, writes = 0;
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
    void unlock() {
      ++rtc->writes;
      rtc->protected_ = false;
    }
    void lock() {
      ++rtc->writes;
      rtc->protected_ = true;
    }
    void flashBoot(bool value) {
      ++rtc->writes;
      assert(!rtc->protected_);
      rtc->boot = value;
      rtc->check();
    }
    void disable() {
      ++rtc->writes;
      assert(!rtc->protected_);
      rtc->enabled = false;
      rtc->check();
    }
    void enable() {
      ++rtc->writes;
      assert(!rtc->protected_);
      rtc->enabled = true;
      rtc->check();
    }
    void systemStage(uint32_t ticks) {
      ++rtc->writes;
      assert(!rtc->protected_);
      rtc->first = ticks;
      rtc->check();
    }
    void rtcStage(uint32_t ticks) {
      ++rtc->writes;
      assert(!rtc->protected_);
      rtc->second = ticks;
      rtc->check();
    }
    void feed() {
      ++rtc->writes;
      assert(!rtc->protected_);
      rtc->elapsed = 0;
      rtc->stage = 0;
    }
  };
}  // namespace
int main() {
  // Regression from native CLR/RTCB trials: an armed shutdown must keep its
  // original timer even when the ISR releases the latch at eight seconds.
  Rtc preserved;
  ensureEmergencyRtc(Registers{&preserved}, true, RESET_STAGE_US, RESET_STAGE_US);
  assert(preserved.writes == 0 && preserved.elapsed == DEADLINE_US);
  preserved.advance(BACKSTOP_US - DEADLINE_US - 1);
  assert(preserved.systemResets == 0);
  preserved.advance(1);
  assert(preserved.systemResets == 1);  // Original ten-second bound, no feed/rearm.
  preserved.advance(RESET_STAGE_US);
  assert(preserved.rtcResets == 1);
  // No active guard (or interrupted initialization/completion) still needs a
  // reset watchdog; an out-of-shutdown long hold must not become latch-only.
  Rtc unarmed;
  disarmRtc(Registers{&unarmed});
  ensureEmergencyRtc(Registers{&unarmed}, false, RESET_STAGE_US, RESET_STAGE_US);
  assert(unarmed.enabled && unarmed.boot && unarmed.elapsed == 0);
  unarmed.advance(RESET_STAGE_US);
  assert(unarmed.systemResets == 1);

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
      << "RTC armed-backstop preservation, unarmed emergency, reconfiguration and disarm passed\n";
}
