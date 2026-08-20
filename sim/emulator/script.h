// Timed scripts, for driving the emulator without a browser.
//
// A script is a text file of "<seconds> <command> [argument]" lines, run against device time, so
// the same script produces the same run whether the clock is pacing to real time or sprinting.
// This is what makes the emulator useful in CI: walk a menu, take screenshots, and assert on what
// the device ended up showing.
//
//   2      press CENTER          press and release a button (also: down / up)
//   2.5    hold CENTER 1800      hold a button for N ms of device time (default 1000)
//   3.5    screenshot out/menu.png
//   4      inject P0,92310       one bus-log line, as if a sensor had reported it
//   5      scenario play         play / pause / seek <seconds>
//   6      expect-page Thermal   fails the run if the device is not on that page
//   7      expect-serial mounted fails the run if that text never appeared on the console
//   8      quit                  stop the emulator (exit code reflects any failed expectation)
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace sim {

  class Script {
   public:
    bool load(const std::string& path, std::string& error);
    bool loaded() const { return !steps_.empty(); }

    // Runs anything due at the current device time.  Called from the device loop.
    void update(uint32_t nowMs);

    uint32_t lastStepMs() const { return steps_.empty() ? 0 : steps_.back().atMs; }
    int failures() const { return failures_; }
    bool quitRequested() const { return quitRequested_; }

   private:
    struct Step {
      uint32_t atMs = 0;
      std::string command;
      std::string argument;
    };

    void run(const Step& step);

    std::vector<Step> steps_;
    size_t nextIndex_ = 0;
    uint64_t serialCursor_ = 0;  // this script's place in the console transcript
    int failures_ = 0;
    bool quitRequested_ = false;
  };

  Script& script();

}  // namespace sim
