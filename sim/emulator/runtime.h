// The emulated device: the firmware's setup()/loop() driven by the virtual clock.
//
// Everything the firmware does happens on one thread (the "device thread"), exactly as it does on
// the ESP32's Arduino loop task.  The HTTP server runs on other threads and never touches
// firmware state directly; it queues commands that the device thread applies between passes
// through loop(), which keeps the emulator free of races the real device cannot have.
#pragma once

#include <stdint.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "display_capture.h"

namespace sim {

  struct Status {
    uint32_t uptimeMs = 0;
    double clockSpeed = 1.0;
    bool paused = false;
    bool poweredOn = false;

    std::string page;  // the display page currently shown
    bool gpsFix = false;
    uint8_t satellites = 0;
    double latitude = 0;
    double longitude = 0;
    float gpsAltitudeM = 0;
    float speedKmh = 0;
    float courseDeg = 0;

    bool baroReady = false;  // false until the first pressure reading arrives
    float altitudeM = 0;     // pressure altitude
    float climbRateMps = 0;
    float pressureHpa = 0;
    float temperatureC = 0;

    // Wind is only estimated while the flight timer runs, and only once enough of a circle has
    // been flown, so these three travel together: without them a missing wind reading is
    // indistinguishable from a broken one.
    bool windValid = false;
    float windSpeedMps = 0;
    float windFromDeg = 0;
    float airspeedMps = 0;

    uint8_t batteryPercent = 0;
    uint32_t batteryMv = 0;
    bool charging = false;
    bool cardMounted = false;
    bool flightTimerRunning = false;
    bool flightLogging = false;
    bool clockSynced = false;  // the firmware has set the device clock from the GPS

    uint32_t toneHz = 0;
    uint8_t volume = 0;

    std::string scenarioName;
    double scenarioPositionS = 0;
    double scenarioLengthS = 0;
    bool scenarioPlaying = false;
  };

  class Runtime {
   public:
    struct Options {
      std::string cardRoot = "sim/sdcard";
      std::string statePath = "sim/state";
      std::string recordingsPath = "sim/recordings";

      // Files copied onto the card at startup if it does not already have them: waypoints,
      // routes, anything a scenario needs to find already on the device.  The card itself
      // is scratch (the firmware writes logs into it), so the reproducible content lives here.
      std::string seedCardPath;
      double speed = 1.0;
      bool powerOnAtBoot = true;  // hold CENTER at boot, as a user turning the device on would
      uint16_t httpPort = 8080;

      // Dismisses the safety disclaimer by pressing DOWN then CENTER when it appears -- the same
      // two presses a user makes.  The firmware is not bypassed; the screen is just answered.
      bool acceptWarning = false;

      // Headless runs stop when the firmware hits a fatal error rather than sitting in its
      // "press a key to reboot" handler forever; interactive runs leave it on screen.
      bool exitOnFatalError = false;
      std::string screenshotOnExit;
      int screenshotScale = 3;
    };

    void configure(const Options& options);
    const Options& options() const { return options_; }

    // Runs the firmware's setup() with the emulated hardware in a plausible resting state.
    void boot();

    // One pass through the firmware loop, plus queued commands and a quantum of virtual time.
    void step();

    // Steps forever; interactive runs never leave it, and the process ends on a signal.
    [[noreturn]] void run();

    // ---------------------------------------------------------------- control (any thread)
    void post(std::function<void()> command);
    // `action` is "down", "up", "click" (a press short enough never to read as a hold) or
    // "hold" (a press of DEFAULT_HOLD_MS).
    void pressButton(const std::string& button, const std::string& action);
    // Presses, then releases after `ms` of device time.  Which of a button's hold actions fires
    // depends on how long it is held: hardware/buttons.cpp reports a hold at 800ms and then
    // increments every 500ms, so hold-to-power-off (increment 2) needs about 1800ms.
    void holdButton(const std::string& button, uint32_t ms);
    static constexpr uint32_t DEFAULT_HOLD_MS = 1000;
    // Publishes one bus-log line (see dispatch/message_injector.h) on the firmware's message bus,
    // applied on the device thread between passes through loop().
    void inject(const std::string& line);
    void setSpeed(double speed);
    void setPaused(bool paused);
    void stepMilliseconds(uint32_t ms);
    void restart();

    // ---------------------------------------------------------------- observation (any thread)
    Frame frame();
    uint64_t frameSequence() const { return frameSequence_; }
    Status status();
    std::vector<std::string> drainSerial();
    std::string screenshotPng(int scale);

    // Writes the exit screenshot, if one was requested.
    void writeExitScreenshot();

   private:
    void drainCommands();
    void releaseDueButtons();
    void answerWarningScreen();
    void captureDisplay();
    void updateStatus();
    static void onClockAdvance();

    Options options_;
    bool booted_ = false;
    bool fatalHandled_ = false;

    // Pause is the user's standing intent; a single step lifts it until virtual time reaches
    // stepUntilUs_, at which point the pause goes back on.
    std::atomic<bool> pauseRequested_{false};
    std::atomic<uint64_t> stepUntilUs_{0};

    std::mutex commandMutex_;
    std::vector<std::function<void()>> commands_;

    struct PendingRelease {
      std::string button;
      uint32_t atMs = 0;  // device time
    };
    std::vector<PendingRelease> pendingReleases_;

    // Where the automatic disclaimer answer has got to.
    enum class WarningStep : uint8_t { Waiting, MovedCursor, Done };
    WarningStep warningStep_ = WarningStep::Waiting;
    uint32_t warningCursorAtMs_ = 0;

    std::mutex stateMutex_;
    Frame frame_;
    uint64_t frameSequence_ = 0;
    Status status_;
    uint32_t lastCaptureMs_ = 0;
  };

  Runtime& runtime();

}  // namespace sim
