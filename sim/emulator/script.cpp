#include "script.h"

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <sstream>

#include "runtime.h"
#include "scenario.h"
#include "sim/board.h"

namespace sim {

  namespace {
    // Console output the script can assert on.  Kept here rather than in the runtime because only
    // expect-serial needs a full transcript; the UI drains its own copy.
    std::string g_transcript;
  }  // namespace

  Script& script() {
    static Script instance;
    return instance;
  }

  bool Script::load(const std::string& path, std::string& error) {
    std::ifstream in(path);
    if (!in) {
      error = "cannot open script " + path;
      return false;
    }

    steps_.clear();
    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
      lineNumber++;
      const size_t comment = line.find('#');
      if (comment != std::string::npos) line = line.substr(0, comment);

      std::istringstream fields(line);
      double seconds = 0;
      Step step;
      if (!(fields >> seconds)) continue;  // blank or comment-only
      if (!(fields >> step.command)) {
        error = path + ":" + std::to_string(lineNumber) + ": missing command";
        return false;
      }
      std::getline(fields, step.argument);
      while (!step.argument.empty() && step.argument.front() == ' ') step.argument.erase(0, 1);
      while (!step.argument.empty() &&
             (step.argument.back() == ' ' || step.argument.back() == '\r')) {
        step.argument.pop_back();
      }
      step.atMs = (uint32_t)(seconds * 1000);
      steps_.push_back(step);
    }

    if (steps_.empty()) {
      error = path + " contains no steps";
      return false;
    }
    return true;
  }

  void Script::update(uint32_t nowMs) {
    // Keep the transcript current for expect-serial.  Draining here means the UI and the script
    // do not compete for the same lines.
    for (const std::string& line : runtime().drainSerial()) {
      printf("%s\n", line.c_str());
      g_transcript += line;
      g_transcript += "\n";
    }

    while (nextIndex_ < steps_.size() && steps_[nextIndex_].atMs <= nowMs) {
      run(steps_[nextIndex_]);
      nextIndex_++;
    }
  }

  void Script::run(const Step& step) {
    Runtime& device = runtime();
    const std::string& argument = step.argument;

    if (step.command == "press" || step.command == "click") {
      device.pressButton(argument, "click");
    } else if (step.command == "down") {
      device.pressButton(argument, "down");
    } else if (step.command == "up") {
      device.pressButton(argument, "up");
    } else if (step.command == "hold") {
      // "hold BUTTON [ms]" -- how long decides which of the button's hold actions fires.
      std::istringstream fields(argument);
      std::string button;
      uint32_t ms = Runtime::DEFAULT_HOLD_MS;
      fields >> button;
      fields >> ms;
      device.holdButton(button, ms);
    } else if (step.command == "screenshot") {
      const std::string png = device.screenshotPng(3);
      FILE* out = fopen(argument.c_str(), "wb");
      if (out) {
        fwrite(png.data(), 1, png.size(), out);
        fclose(out);
        printf("script: wrote %s\n", argument.c_str());
      } else {
        printf("script: could not write %s\n", argument.c_str());
        failures_++;
      }
    } else if (step.command == "inject") {
      device.inject(argument);
    } else if (step.command == "scenario") {
      std::istringstream fields(argument);
      std::string action;
      fields >> action;
      if (action == "play") {
        scenario().play();
      } else if (action == "pause") {
        scenario().pause();
      } else if (action == "seek") {
        double seconds = 0;
        fields >> seconds;
        scenario().seek(seconds);
      } else {
        std::string error;
        if (!scenario().load(argument, error)) {
          printf("script: %s\n", error.c_str());
          failures_++;
        }
      }
    } else if (step.command == "speed") {
      device.setSpeed(atof(argument.c_str()));
    } else if (step.command == "status") {
      // A snapshot of what the device thinks is going on, for watching a value come alive (or
      // not) as a flight progresses.
      const Status s = device.status();
      printf(
          "status %6.1fs page=%-12s fix=%d sats=%2u speed=%5.1fkm/h alt=%7.1fm climb=%+5.2f "
          "timer=%d logging=%d clock=%d wind=%d %.1fm/s from %.0f deg (airspeed %.1f)\n",
          s.uptimeMs / 1000.0, s.page.c_str(), s.gpsFix ? 1 : 0, s.satellites, s.speedKmh,
          s.altitudeM, s.climbRateMps, s.flightTimerRunning ? 1 : 0, s.flightLogging ? 1 : 0,
          s.clockSynced ? 1 : 0, s.windValid ? 1 : 0, s.windSpeedMps, s.windFromDeg, s.airspeedMps);
    } else if (step.command == "expect-page") {
      const std::string page = device.status().page;
      if (page != argument) {
        printf("script: FAIL expected page '%s', got '%s'\n", argument.c_str(), page.c_str());
        failures_++;
      } else {
        printf("script: ok page '%s'\n", page.c_str());
      }
    } else if (step.command == "expect-serial") {
      if (g_transcript.find(argument) == std::string::npos) {
        printf("script: FAIL console never showed '%s'\n", argument.c_str());
        failures_++;
      } else {
        printf("script: ok console showed '%s'\n", argument.c_str());
      }
    } else if (step.command == "quit") {
      quitRequested_ = true;
    } else {
      printf("script: unknown command '%s'\n", step.command.c_str());
      failures_++;
    }
  }

}  // namespace sim
