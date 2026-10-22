#include <ArduinoJson.h>
#include <assert.h>

#include <fstream>
#include <string>

#include "diagnostics/cpu_utilization.h"
#include "dispatch/message_bus.h"
#include "hardware/lc86g.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/board.h"
#include "sim/clock.h"
#include "ui/display/display.h"
#include "ui/settings/settings.h"

extern MessageBus<11> bus;

namespace {
  class StringPrint : public Print {
   public:
    std::string text;
    size_t write(uint8_t c) override {
      text += static_cast<char>(c);
      return 1;
    }
  };
  void runFor(uint64_t us) {
    const uint64_t until = sim::clock().nowUs() + us;
    while (sim::clock().nowUs() < until) sim::runtime().step();
  }
}  // namespace

int main() {
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build/runtime-integration/state";
  options.cardRoot = "sim/build/runtime-integration/card";
  sim::runtime().configure(options);
  sim::runtime().boot();
  sim::scenario().setBus(&bus);
  std::string error;
  assert(sim::scenario().load("sim/recordings/thermal-climb.json", error));
  sim::scenario().play();
  runFor(30000000);
  settings.dev_mode = true;
  settings.disp_showDebugPage = settings.disp_showBasicPage = settings.disp_showUserPage = true;
  settings.disp_showThermalCorePage = settings.disp_showThermalTrackPage =
      settings.disp_showNavPage = true;
  settings.autoSwitchToThermalCorePage = false;
  display.dismissWarning();
  assert(cpu_utilization::start());
  settings.diag_cpuUtilization = true;
  const MainPage pages[] = {MainPage::Basic,       MainPage::User,  MainPage::ThermalCore,
                            MainPage::Navigate,    MainPage::Debug, MainPage::Debug2,
                            MainPage::ThermalTrack};
  for (MainPage page : pages) {
    display.setPage(page);
    runFor(60000000);
  }
  display.setPage(MainPage::Menu);
  runFor(1000000);
  display.setPage(MainPage::User);
  // Stop servicing the loop for 10ms and 20ms during a held button action. Virtual
  // interrupts keep firing. This checks scheduler progress and gap accounting,
  // not the hardware's sensor conversion timing or real display transfer cost.
  sim::runtime().pressButton("RIGHT", "down");
  runFor(40000);
  sim::clock().advanceUs(10000);
  sim::runtime().step();
  runFor(40000);
  sim::clock().advanceUs(20000);
  sim::runtime().step();
  sim::runtime().pressButton("RIGHT", "up");
  runFor(1000000);
  // Exercise the actual UART driver with a sentence split across reads, then a bad checksum.
  sim::scenario().pause();
  const std::string valid = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
  sim::board().gpsFeed(valid.substr(0, 25));
  runFor(2000);
  sim::board().gpsFeed(valid.substr(25) + "\r\n");
  runFor(20000);
  sim::board().gpsFeed(valid.substr(0, valid.size() - 2) + "00\r\n");
  runFor(20000);
  {
    // A deterministic virtual stall during a valid dispatch exercises retained crossing events.
    cpu_utilization::GpsSentenceScope sentence(valid.c_str(), valid.size(), true);
    cpu_utilization::GpsStageScope callback(runtime_capture::GpsStage::ReadingCallbacks);
    bus.receive(GpsMessage(NMEAString(valid.c_str())));
    sim::clock().advanceUs(20000);
  }
  runFor(20000);
  settings.diag_cpuUtilization = false;
  cpu_utilization::freeze();
  StringPrint frozen;
  assert(cpu_utilization::writeJson(frozen));
  JsonDocument capture;
  assert(!deserializeJson(capture, frozen.text));
  std::ofstream out("sim/build/runtime-integration/capture.json");
  out << frozen.text;
  out.close();
  assert(out.good());
  assert(capture["schema_version"].as<int>() == 3);
  assert(capture["gps_profile"]["types"][0]["accepted"].as<unsigned>() > 0);
  assert(capture["gps_profile"]["types"][0]["rejected"].as<unsigned>() > 0);
  assert(capture["gps_profile"]["types"][1]["accepted"].as<unsigned>() > 0);
  bool sawUart = false;
  bool sawSplitUart = false;
  for (JsonObject sentence : capture["gps_profile"]["recent"].as<JsonArray>()) {
    if ((sentence["flags"].as<unsigned>() & runtime_capture::Injected) == 0) {
      sawUart = true;
      assert(sentence["first_read_us"].as<uint64_t>() <= sentence["ready_us"].as<uint64_t>());
      sawSplitUart |=
          sentence["first_read_us"].as<uint64_t>() < sentence["ready_us"].as<uint64_t>();
    }
    uint32_t stages = 0;
    for (uint32_t value : sentence["stage_us"].as<JsonArray>()) stages += value;
    assert(stages <= sentence["processing_us"].as<uint32_t>());
  }
  assert(sawUart && sawSplitUart);
  assert(capture["gps_profile"]["worst"].size() > 0);
  assert(capture["button_slots"]["count"].as<unsigned>() > 0);
  assert(capture["recovery_slots"]["count"].as<unsigned>() > 0);
  assert(capture["button_slots"]["skipped_ticks"].as<unsigned>() >= 1);
  for (JsonObject slot : capture["slots"].as<JsonArray>())
    assert(slot["count"].as<unsigned>() > 400);
  unsigned i = 0;
  for (JsonObject page : capture["pages"].as<JsonArray>()) {
    assert(page["context"].as<unsigned>() == 10 + i++);
    assert(page["count"].as<unsigned>() >= 100);
    assert(page["slots"].as<unsigned>() > 0);
  }
  assert(i == 7);
  puts(
      "All seven page contexts, button exclusions and scheduler recovery after 10/20ms stalls "
      "passed");
}
