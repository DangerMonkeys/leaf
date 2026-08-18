// leafsim: an interactive emulator for the Leaf vario.
//
// Runs the real firmware -- the same src/vario sources that ship on the device -- against a
// virtual ESP32 board, a virtual clock and injected sensor data.  Point a browser at the port it
// serves to get the screen, the buttons and the scenario controls; or run it headless in CI to
// take screenshots and assert on what the device did.

#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <string>
#include <vector>

#include "dispatch/message_bus.h"
#include "http_server.h"
#include "runtime.h"
#include "scenario.h"
#include "script.h"
#include "sim/clock.h"
#include "ui/settings/settings.h"

// The firmware's message bus, defined in src/vario/main.cpp.
extern MessageBus<11> bus;

namespace {

  void printUsage() {
    printf(
        "leafsim -- Leaf device emulator\n"
        "\n"
        "  --port N            HTTP port for the control panel (default 8080, 0 disables)\n"
        "  --sdcard PATH       directory standing in for the SD card (default sim/sdcard)\n"
        "  --state PATH        directory for non-volatile settings (default sim/state)\n"
        "  --recordings PATH   directory scanned for scenarios (default sim/recordings)\n"
        "  --seed-card PATH    copy a directory tree onto the card before boot, for files a\n"
        "                      scenario needs to find there (waypoints, routes)\n"
        "  --scenario FILE     load a recording at startup\n"
        "  --play              start the loaded scenario immediately\n"
        "  --speed X           clock speed: 1 = real time, 10 = ten times faster, 0 = flat out\n"
        "  --charge-mode       boot as if plugged in without the power button held\n"
        "  --accept-warning    answer the safety disclaimer (presses DOWN then CENTER for you)\n"
        "  --setting KEY=VALUE set a saved setting before boot, e.g. LAB_THERM_TRACK=1\n"
        "                      (repeatable; keys are the NVS keys in ui/settings/settings.cpp)\n"
        "  --script FILE       run a timed script of button presses and assertions\n"
        "  --run-seconds N     run for N seconds of device time, then exit (headless runs)\n"
        "  --screenshot FILE   write a PNG of the screen before exiting\n"
        "  --export-log FILE   write the loaded scenario out as a device-format bus log\n"
        "  --scale N           screenshot scale factor (default 3)\n"
        "  --help\n");
  }

  bool argMatches(const char* arg, const char* name) { return strcmp(arg, name) == 0; }

}  // namespace

int main(int argc, char** argv) {
  sim::Runtime::Options options;
  std::string scenarioPath;
  std::string scriptPath;
  std::string screenshotPath;
  std::string exportPath;
  std::vector<std::string> presetSettings;
  bool autoPlay = false;
  double runSeconds = 0;
  int scale = 3;

  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    const char* next = (i + 1 < argc) ? argv[i + 1] : nullptr;

    if (argMatches(arg, "--help") || argMatches(arg, "-h")) {
      printUsage();
      return 0;
    } else if (argMatches(arg, "--port") && next) {
      options.httpPort = (uint16_t)atoi(next);
      i++;
    } else if (argMatches(arg, "--sdcard") && next) {
      options.cardRoot = next;
      i++;
    } else if (argMatches(arg, "--state") && next) {
      options.statePath = next;
      i++;
    } else if (argMatches(arg, "--recordings") && next) {
      options.recordingsPath = next;
      i++;
    } else if (argMatches(arg, "--seed-card") && next) {
      options.seedCardPath = next;
      i++;
    } else if (argMatches(arg, "--scenario") && next) {
      scenarioPath = next;
      i++;
    } else if (argMatches(arg, "--play")) {
      autoPlay = true;
    } else if (argMatches(arg, "--speed") && next) {
      options.speed = atof(next);
      i++;
    } else if (argMatches(arg, "--charge-mode")) {
      options.powerOnAtBoot = false;
    } else if (argMatches(arg, "--accept-warning")) {
      options.acceptWarning = true;
    } else if (argMatches(arg, "--setting") && next) {
      presetSettings.push_back(next);
      i++;
    } else if (argMatches(arg, "--script") && next) {
      scriptPath = next;
      i++;
    } else if (argMatches(arg, "--run-seconds") && next) {
      runSeconds = atof(next);
      i++;
    } else if (argMatches(arg, "--screenshot") && next) {
      screenshotPath = next;
      i++;
    } else if (argMatches(arg, "--export-log") && next) {
      exportPath = next;
      i++;
    } else if (argMatches(arg, "--scale") && next) {
      scale = atoi(next);
      i++;
    } else {
      printf("Unrecognised argument: %s\n\n", arg);
      printUsage();
      return 2;
    }
  }

  options.screenshotOnExit = screenshotPath;
  options.screenshotScale = scale;
  options.exitOnFatalError =
      runSeconds > 0;  // headless runs report and stop; interactive ones wait

  sim::Runtime& device = sim::runtime();
  device.configure(options);

  printf("leafsim: booting the Leaf firmware on a virtual board\n");
  device.boot();

  // Presets are applied after boot, not before.  On a store it has never seen, the firmware writes
  // its whole default set during setup(), and several settings are read back with no fallback
  // (Settings::retrieve calls getBool("AUTO_START") and friends), so a value written beforehand is
  // either overwritten or leaves its neighbours reading as false.  Writing afterwards and asking
  // the firmware to re-read is what a user changing settings in the menus does.
  if (!presetSettings.empty()) {
    Preferences preferences;
    preferences.begin("varioPrefs", false);
    for (const std::string& assignment : presetSettings) {
      const size_t equals = assignment.find('=');
      if (equals == std::string::npos) {
        printf("leafsim: --setting wants KEY=VALUE, got '%s'\n", assignment.c_str());
        return 2;
      }
      const std::string key = assignment.substr(0, equals);
      const std::string value = assignment.substr(equals + 1);
      preferences.putString(key.c_str(), value.c_str());
      printf("leafsim: setting %s = %s\n", key.c_str(), value.c_str());
    }
    preferences.end();
    settings.retrieve();
  }

  // Injected sensor data goes onto the firmware's own message bus, through the same parser the
  // device's UDP injection server uses.
  sim::scenario().setBus(&bus);

  if (!scenarioPath.empty()) {
    std::string error;
    if (!sim::scenario().load(scenarioPath, error)) {
      printf("leafsim: %s\n", error.c_str());
      return 1;
    }
    printf("leafsim: loaded %s (%.1fs)\n", scenarioPath.c_str(), sim::scenario().state().lengthS);
    if (!exportPath.empty() && sim::scenario().exportLog(exportPath)) {
      printf("leafsim: exported %s\n", exportPath.c_str());
    }
    if (autoPlay) sim::scenario().play();
  }

  sim::HttpServer server;
  if (options.httpPort != 0) {
    if (server.start(options.httpPort)) {
      printf("leafsim: control panel on http://localhost:%u\n", options.httpPort);
    } else {
      printf("leafsim: could not listen on port %u\n", options.httpPort);
      return 1;
    }
  }

  if (!scriptPath.empty()) {
    std::string error;
    if (!sim::script().load(scriptPath, error)) {
      printf("leafsim: %s\n", error.c_str());
      return 1;
    }
    // A script without an explicit run length runs until its last step, plus a moment for the
    // device to react to it.
    if (runSeconds <= 0) runSeconds = sim::script().lastStepMs() / 1000.0 + 2.0;
  }

  if (runSeconds > 0) {
    const uint64_t until = sim::clock().nowUs() + (uint64_t)(runSeconds * 1000000.0);
    while (sim::clock().nowUs() < until && !sim::script().quitRequested()) device.step();
    printf("leafsim: ran %.1fs of device time\n", runSeconds);
  } else {
    device.run();
  }

  device.writeExitScreenshot();
  server.stop();

  const int failures = sim::script().failures();
  if (failures > 0) {
    printf("leafsim: %d expectation%s failed\n", failures, failures == 1 ? "" : "s");
    return 1;
  }
  return 0;
}
