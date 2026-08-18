#include "sim/clock.h"

#include <chrono>
#include <thread>

namespace sim {

  namespace {
    constexpr uint64_t PAUSE_POLL_US = 2000;
  }

  Clock& clock() {
    static Clock instance;
    return instance;
  }

  uint64_t Clock::hostUs() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return (uint64_t)duration_cast<microseconds>(steady_clock::now() - start).count();
  }

  void Clock::setSpeed(double speed) {
    speed_ = speed < 0 ? 0 : speed;
    anchored_ = false;
  }

  void Clock::advanceUs(uint64_t us) {
    if (stallHook_) stallHook_();

    // A paused clock blocks the device thread instead of advancing: from the firmware's point of
    // view the world simply stops, which is what makes single-stepping a flight useful.
    while (paused_) {
      std::this_thread::sleep_for(std::chrono::microseconds(PAUSE_POLL_US));
      anchored_ = false;
    }

    const uint64_t target = nowUs_ + us;
    fireDueTimers(target);
    nowUs_ = target;

    if (speed_ <= 0) return;  // free-running: as fast as the host can manage

    if (!anchored_) {
      anchorHostUs_ = hostUs();
      anchorSimUs_ = nowUs_;
      anchored_ = true;
      return;
    }

    const uint64_t simElapsed = nowUs_ - anchorSimUs_;
    const uint64_t hostTarget = anchorHostUs_ + (uint64_t)(simElapsed / speed_);
    const uint64_t hostNow = hostUs();
    if (hostTarget > hostNow) {
      std::this_thread::sleep_for(std::chrono::microseconds(hostTarget - hostNow));
    } else if (hostNow - hostTarget > 250000) {
      // The host fell far behind (a long scenario load, a debugger break).  Re-anchor rather than
      // sprinting to catch up, which would replay a burst of sensor data at the wrong rate.
      anchored_ = false;
    }
  }

  void Clock::fireDueTimers(uint64_t until) {
    for (;;) {
      int soonest = -1;
      uint64_t soonestAt = until;
      for (int i = 0; i < MAX_TIMERS; i++) {
        const Timer& t = timers_[i];
        if (!t.active || !t.callback || t.periodUs == 0) continue;
        if (t.nextUs <= soonestAt) {
          soonest = i;
          soonestAt = t.nextUs;
        }
      }
      if (soonest < 0) return;

      Timer& t = timers_[soonest];
      nowUs_ = soonestAt > nowUs_ ? soonestAt : nowUs_;
      if (t.autoReload) {
        t.nextUs = soonestAt + t.periodUs;
      } else {
        t.periodUs = 0;
      }
      t.callback();
    }
  }

  int Clock::addTimer(uint32_t frequencyHz) {
    for (int i = 0; i < MAX_TIMERS; i++) {
      if (timers_[i].active) continue;
      timers_[i] = Timer{};
      timers_[i].active = true;
      timers_[i].frequencyHz = frequencyHz ? frequencyHz : 1000;
      return i;
    }
    return -1;
  }

  void Clock::setTimerCallback(int handle, TimerCallback callback) {
    if (handle < 0 || handle >= MAX_TIMERS) return;
    timers_[handle].callback = callback;
  }

  void Clock::setTimerAlarm(int handle, uint64_t ticks, bool autoReload) {
    if (handle < 0 || handle >= MAX_TIMERS) return;
    Timer& t = timers_[handle];
    // timerBegin() takes a tick frequency and timerAlarm() a tick count, so the period in
    // microseconds is ticks / frequency, the same arithmetic the ESP timer does.
    t.periodUs = (uint64_t)((double)ticks * 1000000.0 / (double)t.frequencyHz);
    if (t.periodUs == 0) t.periodUs = 1;
    t.autoReload = autoReload;
    t.nextUs = nowUs_ + t.periodUs;
  }

  void Clock::removeTimer(int handle) {
    if (handle < 0 || handle >= MAX_TIMERS) return;
    timers_[handle] = Timer{};
  }

  void Clock::clearTimers() {
    for (int i = 0; i < MAX_TIMERS; i++) timers_[i] = Timer{};
  }

}  // namespace sim
