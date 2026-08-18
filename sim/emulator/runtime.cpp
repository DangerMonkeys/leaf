#include "runtime.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>

#include <dirent.h>
#include <sys/stat.h>

#include <chrono>
#include <fstream>
#include <thread>

#include "hardware/configuration.h"
#include "instruments/ambient.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logging/log.h"
#include "power.h"
#include "scenario.h"
#include "script.h"
#include "sim/board.h"
#include "sim/clock.h"
#include "storage/sd_card.h"
#include "ui/display/display.h"
#include "utils/magic_enum.h"
#include "wind_estimate/wind_estimate.h"

// The firmware's own entry points (src/vario/main.cpp), compiled into the emulator unchanged.
void setup();
void loop();

namespace sim {

  // Set by ESP.restart() / esp_restart().
  extern bool g_restartRequested;
  void setCardRoot(const std::string& root);

  namespace {

    // Virtual time granted per pass through loop().  The firmware's main loop polls flags set by
    // a 10ms timer interrupt, so the quantum has to be well under that to keep the task schedule
    // faithful; 200us gives 50 passes per task block.
    constexpr uint64_t LOOP_QUANTUM_US = 200;

    // The display is redrawn twice a second by the firmware; capturing at 20Hz is more than
    // enough to catch every change without copying the buffer on every pass.
    constexpr uint32_t CAPTURE_INTERVAL_MS = 50;

    // How long a queued "click" holds the button down: long enough to survive the 5ms debounce,
    // short enough that it never turns into a hold (800ms).
    constexpr uint32_t CLICK_HOLD_MS = 60;

    void ensureDirectory(const std::string& path) {
      if (path.empty()) return;
      std::string accumulated;
      size_t start = 0;
      while (start <= path.size()) {
        size_t next = path.find('/', start);
        if (next == std::string::npos) next = path.size();
        accumulated = path.substr(0, next);
        if (!accumulated.empty()) mkdir(accumulated.c_str(), 0777);
        if (next == path.size()) break;
        start = next + 1;
      }
    }

    // Copies a directory tree into the card, leaving anything already there alone: a run that has
    // written its own plans or waypoints keeps them.
    void seedDirectory(const std::string& from, const std::string& to) {
      DIR* dir = opendir(from.c_str());
      if (!dir) return;
      mkdir(to.c_str(), 0777);

      while (struct dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const std::string source = from + "/" + name;
        const std::string target = to + "/" + name;

        struct stat info{};
        if (stat(source.c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) {
          seedDirectory(source, target);
          continue;
        }
        if (stat(target.c_str(), &info) == 0) continue;  // already on the card

        std::ifstream in(source, std::ios::binary);
        std::ofstream out(target, std::ios::binary);
        if (!in || !out) continue;
        out << in.rdbuf();
        printf("leafsim: seeded card with %s\n", name.c_str());
      }
      closedir(dir);
    }

  }  // namespace

  Runtime& runtime() {
    static Runtime instance;
    return instance;
  }

  void Runtime::configure(const Options& options) {
    options_ = options;
    ensureDirectory(options_.cardRoot);
    ensureDirectory(options_.statePath);
    if (!options_.seedCardPath.empty()) seedDirectory(options_.seedCardPath, options_.cardRoot);
    setCardRoot(options_.cardRoot);
    Preferences::simSetStatePath(options_.statePath);
    clock().setSpeed(options_.speed);

    // The hook runs on every clock advance, including from inside the firmware's fatal-error
    // handler, which never returns.  That is the only vantage point from which a headless run can
    // notice the device has stopped and report what happened.
    if (options_.exitOnFatalError) clock().setStallHook(&Runtime::onClockAdvance);
  }

  void Runtime::onClockAdvance() {
    if (!fatalErrorSeen()) return;
    Runtime& device = runtime();
    if (device.fatalHandled_) return;
    device.fatalHandled_ = true;

    device.captureDisplay();
    device.writeExitScreenshot();
    fflush(stdout);
    fprintf(stderr, "leafsim: device halted -- %s\n", fatalErrorMessage().c_str());
    exit(3);
  }

  void Runtime::writeExitScreenshot() {
    if (options_.screenshotOnExit.empty()) return;
    const std::string png = screenshotPng(options_.screenshotScale);
    FILE* out = fopen(options_.screenshotOnExit.c_str(), "wb");
    if (!out) return;
    fwrite(png.data(), 1, png.size(), out);
    fclose(out);
    printf("leafsim: wrote %s\n", options_.screenshotOnExit.c_str());
  }

  void Runtime::boot() {
    // A reboot re-runs setup(), which installs the task timers again.  The old ones have to go
    // first, or the device would run its task schedule twice over.  (Firmware statics do not
    // reset, so this is a warm restart, not a power cycle.)
    clock().clearTimers();

    Board& b = board();

    // A resting board: battery charged, USB attached, card inserted, no buttons down.
    b.setBatteryMilliVolts(4000);
#ifdef ISET
    b.setAdcMilliVolts(ISET, 0);  // no charge current
#endif

    // Holding CENTER through boot is how a user turns a Leaf on; without it the firmware boots
    // into charge mode, which is also worth being able to emulate.
    if (options_.powerOnAtBoot) sim::pressButton("CENTER", true);

    setup();
    booted_ = true;

    if (options_.powerOnAtBoot) sim::pressButton("CENTER", false);

    captureDisplay();
    updateStatus();
  }

  void Runtime::step() {
    if (!booted_) boot();

    loop();
    drainCommands();
    if (options_.acceptWarning) answerWarningScreen();
    scenario().update(clock().millis());
    if (script().loaded()) script().update(clock().millis());

    clock().advanceUs(LOOP_QUANTUM_US);

    // A single step ends when virtual time has moved far enough, putting the pause back.
    if (stepUntilUs_ != 0 && clock().nowUs() >= stepUntilUs_) {
      stepUntilUs_ = 0;
      if (pauseRequested_) clock().setPaused(true);
    }

    const uint32_t now = clock().millis();
    if (now - lastCaptureMs_ >= CAPTURE_INTERVAL_MS) {
      lastCaptureMs_ = now;
      captureDisplay();
      updateStatus();
    }

    if (g_restartRequested) {
      g_restartRequested = false;
      Serial.println("--- emulated device restarting ---");
      booted_ = false;
    }
  }

  void Runtime::run() {
    for (;;) step();
  }

  // ---------------------------------------------------------------- control

  void Runtime::post(std::function<void()> command) {
    std::lock_guard<std::mutex> lock(commandMutex_);
    commands_.push_back(std::move(command));
  }

  void Runtime::drainCommands() {
    std::vector<std::function<void()>> pending;
    {
      std::lock_guard<std::mutex> lock(commandMutex_);
      pending.swap(commands_);
    }
    for (auto& command : pending) command();
    releaseDueButtons();
  }

  void Runtime::pressButton(const std::string& button, const std::string& action) {
    // Pressing a button is a pin write, not a call into firmware, so it is done immediately from
    // whichever thread asked rather than queued.  That matters when the device has halted in the
    // fatal-error handler: a key press is the only way out, and the device loop is not running to
    // drain a queue.
    if (action == "down") {
      sim::pressButton(button, true);
      return;
    }
    if (action == "up") {
      sim::pressButton(button, false);
      return;
    }

    holdButton(button, action == "hold" ? DEFAULT_HOLD_MS : CLICK_HOLD_MS);
  }

  void Runtime::holdButton(const std::string& button, uint32_t ms) {
    // Press now, release after a window of device time.  The release is timed by the device loop
    // rather than by a host timer, because at high clock speeds a host timer would overshoot by
    // seconds of device time -- long enough for a click to read as a hold, which is how a Leaf is
    // powered off.
    sim::pressButton(button, true);
    std::lock_guard<std::mutex> lock(commandMutex_);
    pendingReleases_.push_back({button, clock().millis() + ms});
  }

  void Runtime::releaseDueButtons() {
    std::vector<std::string> due;
    {
      std::lock_guard<std::mutex> lock(commandMutex_);
      const uint32_t now = clock().millis();
      for (size_t i = 0; i < pendingReleases_.size();) {
        if ((int32_t)(now - pendingReleases_[i].atMs) >= 0) {
          due.push_back(pendingReleases_[i].button);
          pendingReleases_.erase(pendingReleases_.begin() + i);
        } else {
          i++;
        }
      }
    }
    for (const std::string& button : due) sim::pressButton(button, false);
  }

  void Runtime::answerWarningScreen() {
    if (warningStep_ == WarningStep::Done) return;

    // Driven off what the firmware last drew, so it answers the screen when the screen is up
    // rather than at a guessed moment.  DOWN puts the cursor on ACCEPT, CENTER confirms it.
    const bool showingWarning = display.lastRenderContext() == DisplayRenderContext::Warning;

    if (warningStep_ == WarningStep::Waiting) {
      if (!showingWarning) return;
      pressButton("DOWN", "click");
      warningCursorAtMs_ = clock().millis();
      warningStep_ = WarningStep::MovedCursor;
      return;
    }

    // Wait for the click to be released and read before sending the next one.
    if (clock().millis() - warningCursorAtMs_ < 300) return;
    if (!showingWarning) {
      warningStep_ = WarningStep::Done;  // dismissed by a real user in the meantime
      return;
    }
    pressButton("CENTER", "click");
    warningStep_ = WarningStep::Done;
  }

  void Runtime::inject(const std::string& line) {
    post([line] {
      // Shares the scenario player's bus; the parser itself is the firmware's, so a hand-injected
      // line behaves exactly like the same line arriving over UDP or from a recording.
      static MessageInjector injector;
      injector.setBus(scenario().injectorBus());
      injector.handleLine(line.c_str(), line.size());
    });
  }

  void Runtime::setSpeed(double speed) { clock().setSpeed(speed); }

  void Runtime::setPaused(bool paused) {
    pauseRequested_ = paused;
    stepUntilUs_ = 0;
    clock().setPaused(paused);
  }

  void Runtime::stepMilliseconds(uint32_t ms) {
    // Single-stepping is a temporary lift of the pause, ended by the device loop once virtual time
    // has moved far enough.  Doing it that way rather than running the loop from here keeps the
    // firmware being driven from exactly one place.
    stepUntilUs_ = clock().nowUs() + (uint64_t)ms * 1000;
    clock().setPaused(false);
  }

  void Runtime::restart() { g_restartRequested = true; }

  // ---------------------------------------------------------------- observation

  void Runtime::captureDisplay() {
    Frame captured = captureFrame();
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!(captured == frame_)) {
      frame_ = std::move(captured);
      frameSequence_++;
    }
  }

  void Runtime::updateStatus() {
    Status s;
    s.uptimeMs = clock().millis();
    s.clockSpeed = clock().speed();
    s.paused = clock().paused();
    s.poweredOn = power.info().onState == PowerState::On;
    s.page = nameOf(display.getPage()).c_str();

    s.gpsFix = gps.location.isValid();
    s.satellites = (uint8_t)gps.satellites.value();
    s.latitude = gps.location.lat();
    s.longitude = gps.location.lng();
    s.gpsAltitudeM = (float)gps.altitude.meters();
    s.speedKmh = (float)gps.speed.kmph();
    s.courseDeg = (float)gps.course.deg();

    // The instruments assert on being read before they have data -- the same guard the firmware's
    // own display code uses -- so the status snapshot has to respect their state.
    if (baro.state() == Barometer::State::Ready) {
      s.baroReady = true;
      s.altitudeM = baro.altF();
      s.pressureHpa = (float)baro.pressure().millibars();
      if (baro.climbRateFilteredValid()) s.climbRateMps = baro.climbRateFiltered() / 100.0f;
    }
    if (ambient.state() != Ambient::State::NoData) {
      s.temperatureC = ambient.temp();
    }

    s.batteryPercent = (uint8_t)power.info().batteryPercent;
    s.batteryMv = power.info().batteryMV;
    s.charging = power.info().charging;
    s.cardMounted = sdcard.isMounted();
    s.flightTimerRunning = flightTimer_isRunning();
    s.flightLogging = flightTimer_isLogging();
    s.clockSynced = systemClockSynced();

    const WindEstimate& wind = windEstimator.getWindEstimate();
    s.windValid = wind.validEstimate;
    s.windSpeedMps = wind.windSpeed;
    s.windFromDeg = wind.windDirectionFrom * 180.0f / (float)M_PI;
    s.airspeedMps = wind.airspeed;

    s.toneHz = board().currentTone();
    s.volume = board().volume();

    const Scenario::State scenarioState = scenario().state();
    s.scenarioName = scenarioState.name;
    s.scenarioPositionS = scenarioState.positionS;
    s.scenarioLengthS = scenarioState.lengthS;
    s.scenarioPlaying = scenarioState.playing;

    std::lock_guard<std::mutex> lock(stateMutex_);
    status_ = std::move(s);
  }

  Frame Runtime::frame() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return frame_;
  }

  Status Runtime::status() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return status_;
  }

  std::vector<std::string> Runtime::drainSerial() { return HostConsole::drainLines(); }

  std::string Runtime::screenshotPng(int scale) { return encodePng(frame(), scale); }

}  // namespace sim
