#include <Arduino.h>
#include <ArduinoJson.h>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "comms/ble.h"
#include "dispatch/message_bus.h"
#include "hardware/configuration.h"
#include "logging/log.h"
#include "power.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/board.h"
#include "sim/clock.h"
#include "system/shutdown_guard.h"
#include "ui/settings/settings.h"
extern MessageBus<11> bus;
using namespace shutdown_guard;
void run(unsigned ms) {
  const auto end = sim::clock().millis() + ms;
  while (sim::clock().millis() < end) sim::runtime().step();
}
bool completeFlight(const std::string& card) {
  const auto dir = std::filesystem::path(card) / "logbook";
  if (!std::filesystem::exists(dir)) return false;
  for (const auto& p : std::filesystem::directory_iterator(dir)) {
    if (p.path().extension() != ".json") continue;
    JsonDocument d;
    std::ifstream f(p.path());
    if (!deserializeJson(d, f) && d["complete"].as<bool>()) return true;
  }
  return false;
}
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string mode = argv[1];
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  // Fresh fixtures prevent a previous run's completed flight masking a failed save.
  std::filesystem::create_directories("sim/build-tests/shutdown");
  std::string fixture = "sim/build-tests/shutdown/" + mode + "-XXXXXX";
  assert(mkdtemp(fixture.data()));
  options.cardRoot = fixture + "/card";
  options.statePath = fixture + "/state";
  sim::runtime().configure(options);
  if (mode == "recovery") {
    const std::string marker = std::to_string(uint32_t(Stage::BleDeinit)) + ":" +
                               std::to_string(uint32_t(Reason::Deadline));
    setenv("LEAF_SIM_SHUTDOWN_RECOVERY", marker.c_str(), 1);
    settings.init();
    settings.dev_mode = true;
    settings.diag_systemEvents = true;
    settings.boot_toOnState = true;
    settings.system_bluetoothOn = true;
    settings.save();
  }
  sim::runtime().boot();
  settings.dev_mode = true;
  settings.system_autoOff = 0;
  if (mode == "recovery") {
    assert(recovering() && power.info().onState == PowerState::OffUSB);
    assert(digitalRead(LATCH_PIN) == LOW && !BLE::get().isStarted());
    assert(!settings.boot_toOnState);
    std::ifstream events(options.cardRoot + "/diagnostics/system_events.csv");
    const std::string csv((std::istreambuf_iterator<char>(events)), {});
    assert(csv.find("\"shutdown\",\"recovered\",\"ble-deinit\",\"reason\",1") != std::string::npos);
    // The button held through reboot must be released before a new hold counts.
    sim::pressButton("CENTER", true);
    sim::clock().advanceUs(HOLD_US + 20000);
    sim::pressButton("CENTER", false);
    sim::clock().advanceUs(20000);
    sim::pressButton("CENTER", true);
    try {
      sim::clock().advanceUs(HOLD_US + 20000);
      assert(false);
    } catch (const EmergencyReset&) {
      assert(lastReason() == Reason::LongHold);
    }
    std::cout << "Recovery overrides held center; fresh long hold works after release\n";
    return 0;
  }
  assert(!recovering() && power.info().onState == PowerState::On);
  assert(digitalRead(LATCH_PIN) == HIGH);
  sim::scenario().setBus(&bus);
  std::string error;
  assert(sim::scenario().load("sim/recordings/thermal-climb.json", error));
  sim::scenario().play();
  run(4000);
  flightTimer_start();
  run(2000);
  if (mode == "hold") {
    sim::pressButton("CENTER", true);
    const auto start = sim::clock().nowUs();
    try {
      sim::clock().advanceUs(HOLD_US + 20000);
      assert(false);
    } catch (const EmergencyReset&) {
      assert(lastReason() == Reason::LongHold);
      assert(sim::clock().nowUs() - start <= HOLD_US + 10000);
      assert(digitalRead(LATCH_PIN) == LOW);
    }
    std::cout << "Long hold works with main loop completely unserviced\n";
    return 0;
  }
  Stage injected = Stage::None;
  if (mode == "lock") injected = Stage::Lock;
  if (mode == "save" || mode == "watchdog" || mode == "rtc") injected = Stage::Flight;
  if (mode == "worker") injected = Stage::BleWorker;
  if (mode == "sound") injected = Stage::Sound;
  if (mode == "peripherals") injected = Stage::Peripherals;
  if (mode == "ble") injected = Stage::BleDeinit;
  if (mode == "disabled") {
    injected = Stage::Lock;
    settings.dev_mode = false;
  }
  const bool rtcBackstop = mode == "watchdog" || mode == "rtc";
  setFault(injected, mode == "watchdog" ? FaultBehavior::InterruptsOff
                     : mode == "rtc"    ? FaultBehavior::RtcOnly
                                        : FaultBehavior::Stall);
  const auto start = sim::clock().nowUs();
  if (mode == "nested") {
    begin();
    sim::clock().advanceUs(4000000);
    try {
      Lease inner(Stage::BleDeinit);
      sim::clock().advanceUs(4020000);
      assert(false);
    } catch (const EmergencyReset&) {
      assert(sim::clock().nowUs() - start <= DEADLINE_US + 10000);
      assert(lastReason() == Reason::Deadline);
    }
    std::cout << "Nested cleanup cannot extend the outer deadline\n";
    return 0;
  }
  try {
    power.shutdown();
    assert(mode == "normal" || mode == "disabled");
    assert(power.info().onState == PowerState::OffUSB);
    assert(digitalRead(LATCH_PIN) == LOW);
    assert(completeFlight(options.cardRoot));
    assert(sim::clock().nowUs() - start < DEADLINE_US);
    // The old deadline must be disarmed after a successful charging transition.
    sim::clock().advanceUs(BACKSTOP_US + 20000);
    std::cout << "Normal shutdown saved flight and returned to charging within deadline\n";
  } catch (const EmergencyReset&) {
    assert(mode != "normal" && mode != "disabled");
    assert(digitalRead(LATCH_PIN) == LOW);
    assert(lastReason() == (rtcBackstop ? Reason::HardwareWatchdog : Reason::Deadline));
    const uint32_t bound = rtcBackstop ? BACKSTOP_US : DEADLINE_US;
    assert(sim::clock().nowUs() - start <= bound + 10000);
    if (mode == "worker" || mode == "sound" || mode == "peripherals" || mode == "ble")
      assert(completeFlight(options.cardRoot));
    std::cout << "Injected " << mode << " stall released latch at "
              << (sim::clock().nowUs() - start) / 1000 << " ms\n";
  }
}
