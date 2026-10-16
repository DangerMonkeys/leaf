#include <Arduino.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <fstream>

#include "dispatch/message_bus.h"
#include "free_flight.h"
#include "navigation/thermal_core.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"

extern MessageBus<11> bus;

int main() {
  sim::FreeFlightModel a;
  a.reset();
  double cx, cy, radius, strength;
  a.profile(1000, cx, cy, radius, strength);
  assert(fabs(cx) < .001 && fabs(cy) < .001);
  a.elapsedS = 300;
  a.profile(1000, cx, cy, radius, strength);
  assert(fabs(cx) < .001 && fabs(cy) < .001);  // source stays fixed as time passes
  a.profile(1100, cx, cy, radius, strength);
  assert(fabs(hypot(cx, cy) - 50) < .01);
  a.parameters.drifting = true;
  a.profile(1000, cx, cy, radius, strength);
  assert(fabs(hypot(cx, cy) - 600) < .01);
  a.parameters.drifting = false;
  a.elapsedS = 0;
  const double edge = a.liftAt(60, 0, 1000);
  a.parameters.peakiness = 4;
  assert(a.liftAt(60, 0, 1000) < edge);
  assert(fabs(a.liftAt(0, 0, 1000) - 4) < .001);
  a.parameters.diameterM = 300;
  assert(a.liftAt(60, 0, 1000) > edge);
  a.reset();
  auto b = a;
  for (int i = 0; i < 40; ++i) {
    a.step(.05, 1, 0);
    b.step(.05, 1, 0);
  }
  assert(a.turnRateDps > 27 && a.turnRateDps < 29);
  assert(a.x == b.x && a.y == b.y && a.z == b.z);
  a.reset(1700);
  a.profile(1700, cx, cy, radius, strength);
  assert(a.z == 1700 && a.sourceAltM == 1700 && fabs(cx) < .001 && fabs(cy) < .001);

  auto& device = sim::runtime();
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/free-flight-test/state";
  options.cardRoot = "sim/build-tests/free-flight-test/card";
  device.configure(options);
  device.boot();
  sim::scenario().setBus(&bus);
  auto& flight = sim::freeFlight();
  flight.start(sim::clock().millis(), &bus);
  uint32_t until = sim::clock().millis() + 1400;
  flight.controls(1, 0);
  while (sim::clock().millis() < until) device.step();
  flight.controls(0, 0);
  until = sim::clock().millis() + 90000;
  while (sim::clock().millis() < until) device.step();
  const auto s = flight.snapshot();
  assert(s.active && s.trail.size() > 100);
  assert(device.status().gpsFix && device.status().baroReady);
  assert(device.status().scenarioName == "Free flight");
  assert(device.status().windValid);  // actual firmware estimates wind from synthesized GPS
  assert(fabs(device.status().windSpeedMps - 2) < .6);
  device.setPaused(true);
  assert(device.status().paused);
  const auto paused = flight.snapshot().model.elapsedS;
  flight.configure(s.model.parameters);
  assert(flight.snapshot().model.elapsedS == paused);  // parameter changes never advance physics
  device.setPaused(false);

  const std::string log = flight.recording();
  assert(log.find("GNGGA") != std::string::npos && log.find("GNRMC") != std::string::npos);
  const char* file = "sim/build-tests/free-flight-test/flight.log";
  {
    std::ofstream out(file);
    out << log;
  }
  std::string error;
  flight.stop();
  assert(sim::scenario().load(file, error));
  assert(sim::scenario().track().size() > 80);
  assert(!flight.snapshot().active);
  puts("Free flight physics, sensor integration, wind estimation, pause and replay checks passed");
}
