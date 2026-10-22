#include <assert.h>
#include <cstdio>
#include "dispatch/message_bus.h"
#include "instruments/baro.h"
#include "navigation/thermal_core.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/settings/settings.h"
extern MessageBus<11> bus;
int main() {
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build/runtime-wake-repro/state";
  options.cardRoot = "sim/build/runtime-wake-repro/card";
  sim::runtime().configure(options);
  sim::runtime().boot();
  settings.disp_showThermalCorePage = true;
  sim::scenario().setBus(&bus);
  std::string error;
  assert(sim::scenario().load("sim/recordings/thermal-core.json", error));
  sim::scenario().play();
  const uint64_t until = sim::clock().nowUs() + 30000000;
  while (sim::clock().nowUs() < until) sim::runtime().step();
  sim::scenario().pause();
  const auto samples = thermalTracker.recentCoreSamples().size();
  assert(samples > 0);
  assert(baro.state() == Barometer::State::Ready);
  std::printf("REPRO: cached thermal samples=%u; barometer ready before sleep\n", unsigned(samples));
  baro.sleep();
  baro.wake();
  assert(baro.state() == Barometer::State::WaitingForFirstReading);
  assert(thermalTracker.recentCoreSamples().size() == samples);
  std::puts("REPRO: barometer waiting after wake, thermal samples retained; call ThermalCore::update");
  thermalCore.update();
  std::puts("REPRO: unexpectedly returned without reproducing fatal error");
  return 1;
}
