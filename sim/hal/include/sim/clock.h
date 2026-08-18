// The emulator's virtual clock.
//
// millis()/micros()/delay() all read from here rather than from the host clock, which is what
// lets a scenario run faster than real time, pause mid-flight, or step forward deterministically.
// Time only moves when the device thread lets it: inside delay(), or by the quantum the run loop
// advances after each pass through the firmware's loop().
#pragma once

#include <stdint.h>

namespace sim {

  // Hardware timer interrupts (the 10ms task timer and 500ms charge timer the firmware installs).
  // Registered callbacks fire from whichever call advanced virtual time past their deadline, which
  // is the same place the device sees them: in the middle of the main loop.
  using TimerCallback = void (*)();

  class Clock {
   public:
    uint64_t nowUs() const { return nowUs_; }
    uint32_t millis() const { return (uint32_t)(nowUs_ / 1000); }
    uint32_t micros() const { return (uint32_t)nowUs_; }

    // Moves virtual time forward, firing any timer callbacks due along the way, and blocks in real
    // time if the clock is pacing to a speed multiplier.
    void advanceUs(uint64_t us);

    // Speed multiplier: 1.0 tracks wall clock, 10.0 runs ten times faster, 0 runs flat out.
    void setSpeed(double speed);
    double speed() const { return speed_; }

    // Paused clocks refuse to advance, so the firmware sees time standing still.
    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    // Runs one timer slot; returns the handle index used by timerBegin()/timerAlarm().
    int addTimer(uint32_t frequencyHz);
    void setTimerCallback(int handle, TimerCallback callback);
    void setTimerAlarm(int handle, uint64_t ticks, bool autoReload);
    void removeTimer(int handle);

    // Drops every timer.  Used when the emulated device reboots: setup() installs its timers
    // again, and without this the old ones would keep firing and double the task rate.
    void clearTimers();

    // Real (host) microseconds since the emulator started, for pacing and UI timing.
    static uint64_t hostUs();

    // Called on every advance, including those from inside firmware code that will never return
    // (the fatal-error handler waits for a key press).  That makes it the only place a headless
    // run can notice the device has halted.
    using StallHook = void (*)();
    void setStallHook(StallHook hook) { stallHook_ = hook; }

   private:
    void fireDueTimers(uint64_t until);

    struct Timer {
      bool active = false;
      uint32_t frequencyHz = 1000;
      uint64_t periodUs = 0;
      uint64_t nextUs = 0;
      bool autoReload = true;
      TimerCallback callback = nullptr;
    };

    static constexpr int MAX_TIMERS = 8;
    Timer timers_[MAX_TIMERS];

    uint64_t nowUs_ = 0;
    double speed_ = 1.0;
    bool paused_ = false;
    StallHook stallHook_ = nullptr;

    // Wall-clock anchor for pacing; reset whenever speed changes or the clock is unpaused.
    uint64_t anchorHostUs_ = 0;
    uint64_t anchorSimUs_ = 0;
    bool anchored_ = false;
  };

  Clock& clock();

  // True once the firmware has set the device clock from a GPS fix (see hal/src/system_clock.cpp).
  bool systemClockSynced();

}  // namespace sim
