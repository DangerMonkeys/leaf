#include <cassert>
#include <cmath>
#include <cstdio>

#include "instruments/g_load_tracker.h"
#include "logging/flight_auto_start.h"

int main() {
  FlightAutoStart launch;
  GLoadTracker load;
  load.startFlight();  // Manual timer start while preparing on the ground.
  FlightAutoStart::Sample s;
  s.speedValid = s.positionValid = s.altitudeValid = s.climbValid = true;
  s.latitude = 37;
  s.longitude = -122;
  s.altitudeM = 1000;
  uint32_t t = 1000;
  auto second = [&](bool activity, bool valid = true, float g = 1) {
    const bool confirmed = launch.update(t, s);
    load.updateFlightState(confirmed, activity, valid);
    if (load.diagnosticState().transition == GLoadTracker::Transition::Landed) launch.reset();
    for (uint32_t dt = 0; dt < 1000; dt += 50) load.addSample(g, t + dt);
    t += 1000;
  };
  // Handling-like climb estimates lack matching pressure departure. The old
  // three-check activity rule would have armed, but no flight may be committed.
  s.climbMps = 1;
  for (int i = 0; i < 120; ++i) second(true, true, 2);
  assert(!load.diagnosticState().airborne && !load.valid());
  // A corroborated forward launch enters on the same confirmed check, without
  // another three-check debounce. Keep the guard armed while speed stays high.
  s.climbMps = 0;
  s.speedMps = 5;
  bool entered = false;
  for (int i = 0; i < 10; ++i) {
    s.latitude += 5.0 / 111195;
    second(true);
    if (launch.triggered()) {
      assert(load.diagnosticState().airborne);
      entered = true;
    }
  }
  assert(entered);
  for (int i = 0; i < 100; ++i) {
    s.latitude += 5.0 / 111195;
    second(true);
  }
  assert(load.valid() && load.maxG() < 1.01f);  // Earlier handling excluded.
  // Failure of the launch test does not terminate slow, level flight.
  s.speedMps = 0;
  for (int i = 0; i < 10; ++i) second(false);
  assert(!launch.triggered() && load.diagnosticState().airborne);
  for (int i = 0; i < 30; ++i) second(false, false);
  assert(load.diagnosticState().airborne);  // Missing input cannot prove landing.
  for (int i = 0; i < 20; ++i) second(false);
  assert(!load.diagnosticState().airborne);
  // Fresh vertical evidence works without GPS, and a relaunch gets a new guard.
  s.speedValid = s.positionValid = false;
  s.climbMps = 1.5;
  for (int i = 0; i < 8; ++i) {
    s.altitudeM += 1.5;
    second(true);
  }
  assert(load.diagnosticState().airborne && load.diagnosticState().ignoreRemainingMs > 0);
  // An automatically started timer can consume already-confirmed evidence.
  GLoadTracker automatic;
  automatic.startFlight();
  automatic.updateFlightState(launch.triggered() != 0, true);
  assert(automatic.diagnosticState().airborne);
  assert(automatic.diagnosticState().ignoreRemainingMs == 15000);
  // A false automatic timer start may still happen (Blue's ground-only case).
  // A short accepted episode is discarded; later uncorroborated activity must
  // not independently rearm the weaker old g-load detector and commit handling.
  for (uint32_t at = 1000; at < 121000; at += 50) {
    if (at % 1000 == 0) automatic.updateFlightState(false, at > 30000);
    automatic.addSample(1.5f, at);
  }
  assert(!automatic.diagnosticState().airborne && !automatic.valid());
  puts("Shared launch evidence, manual/automatic starts, slow flight and relaunch passed");
}
