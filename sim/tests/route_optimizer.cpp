#include "navigation/route_optimizer.h"
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include "../route_solver/guidance.h"
#include "../route_solver/solver.h"
#include "dispatch/message_bus.h"
#include "display_capture.h"
#include "free_flight.h"
#include "instruments/gps.h"
#include "logging/log.h"
#include "navigation/gpx.h"
#include "navigation/optimized_route_guidance.h"
#include "navigation/route_store.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "system/task_timer.h"
#include "ui/display/display.h"
#include "ui/settings/settings.h"
extern MessageBus<11> bus;
using namespace route_optimizer;

namespace {
  unsigned timerCallbacks = 0;
  void timerCallback() { ++timerCallbacks; }
}  // namespace
void timerTests() {
  assert(task_timer::hasHeadroom(0, 0));
  assert(task_timer::hasHeadroom(7998, 0));
  assert(!task_timer::hasHeadroom(7999, 0));
  assert(!task_timer::hasHeadroom(10000, 0));
  assert(!task_timer::hasHeadroom(0, 10000));
  assert(!task_timer::hasHeadroom(7000, 1000));
  task_timer::CaptureReference reference;
  reference.reset(0);
  assert(reference.expected(100, 1023123, 2123) == 1021000);
  assert(reference.expected(101, 1033123, 2123) == 1031000);
  // Lost callbacks before capture are calibrated out. Loss during capture is
  // deliberately retained in expected/actual timestamps, rather than hidden.
  assert(reference.expected(102, 1063123, 2123) == 1041000);
  reference.reset(0);
  assert(reference.expected(102, 1063123, 2123) == 1061000);
  sim::clock().setSpeed(0);
  auto* timer = timerBegin(task_timer::FREQUENCY_HZ);
  assert(timer);
  timerAttachInterrupt(timer, timerCallback);
  timerAlarm(timer, task_timer::PERIOD_US, true, 0);
  sim::clock().advanceUs(1234);
  assert(timerRead(timer) == 1234);
  sim::clock().advanceUs(8766);
  assert(timerCallbacks == 1 && timerRead(timer) == 0);
  sim::clock().maskTimerInterrupts(true);
  sim::clock().advanceUs(22123);
  assert(timerCallbacks == 1 && timerRead(timer) == 2123);
  sim::clock().maskTimerInterrupts(false);
  assert(timerCallbacks == 2 && timerRead(timer) == 2123);
  timerEnd(timer);
  std::cout
      << "Timer phase, budget boundaries, coalesced interrupts and capture reference passed\n";
}
void geometryTests() {
  float margin = 0;
  const Disk disk{{0, 0}, 100};
  conservativePoint({0, -100}, {-1000, -100}, disk, &margin);
  assert(std::abs(margin - 20) < .001);
  conservativePoint({0, -100}, {-1000, -1100}, disk, &margin);
  assert(margin < .001);
  conservativePoint({0, -100}, {0, -1100}, disk, &margin);
  assert(margin == 0);
  const auto small = conservativePoint({0, -10}, {-100, -10}, {{0, 0}, 10});
  assert(length(small) == 0);
  const auto equal = conservativePoint({0, -20}, {0, -200}, {{0, 0}, 20});
  assert(length(equal) == 20);
  std::mt19937 rng(42);
  double worstEntry = 0, worstRoundtrip = 0;
  for (Geo center : {Geo{47.51, 11.18}, Geo{84, 179.99}, Geo{-75, -179.99}, Geo{0, 0}}) {
    Projection projection;
    projection.reset(center);
    for (int i = 0; i < 500; ++i) {
      const double radius = 10 + rng() % 5000, heading = rng() % 360;
      const Geo pilot = destination(center, heading, radius + 100 + rng() % 20000);
      // A varying inward aim, including near-glancing approaches.
      const Geo aim = destination(center, heading + 45 + rng() % 180, radius * .999);
      const double entry = entryDistance(pilot, aim, center, radius);
      assert(std::isfinite(entry));
      const Geo crossing = destination(pilot, bearing(pilot, aim), entry);
      worstEntry = std::max(worstEntry, std::abs(distance(crossing, center) - radius));
      assert(std::abs(gps.distanceBetween(crossing.lat, crossing.lon, center.lat, center.lon) -
                      radius) < .002);
      Vec projected;
      assert(projection.forward(pilot, projected));
      const Geo restored = projection.inverse(projected);
      worstRoundtrip = std::max(worstRoundtrip, distance(pilot, restored));
    }
  }
  assert(worstEntry < .002 && worstRoundtrip < .02);
  // Cross-date-line, polar and large-cylinder geometry distortion are explicit.
  Projection p;
  p.reset({80, 179.99});
  Vec v;
  assert(p.forward({80, -179.99}, v));
  assert(length(v) < 500);
  assert(!p.forward({-80, 0}, v));
  assert(!p.supports({{190000, 0}, 20000}));
  // Finite input failure is a status, not an exception on the device.
  Solver<20> invalid;
  invalid.disks[0] = {{NAN, 0}, 1};
  assert(!invalid.initialize(1, {}));
  assert(!invalid.initialize(21, {}));
  std::cout << "Geographic crossings: " << worstEntry << " m max error; roundtrip "
            << worstRoundtrip << " m\n";
}
void solverTests() {
  std::mt19937 rng(12345);
  double maximumDifference = 0;
  for (int trial = 0; trial < 180; ++trial) {
    const size_t count = trial % 3 == 0 ? 20 : 6 + rng() % 5;
    route_experiment::Disk input[20];
    Solver<20> device;
    for (size_t i = 0; i < count; ++i) {
      input[i] = {{float(int(rng() % 20000) - 10000), float(int(rng() % 20000) - 10000)},
                  float(rng() % 1500)};
      device.disks[i] = {{input[i].center.x, input[i].center.y}, input[i].radius};
    }
    const Vec start{-12000, -12000};
    assert(device.initialize(count, start));
    route_experiment::PrimalDual<20> desktop;
    desktop.initialize(input, count, {start.x, start.y});
    for (int i = 0; i < 256; ++i) {
      assert(device.iterate());
      desktop.improve(1);
    }
    maximumDifference = std::max(maximumDifference,
                                 double(std::abs(device.bestDistance() - desktop.best_distance())));
    assert(std::abs(device.bestDistance() - desktop.best_distance()) < .1);
    for (size_t i = 0; i < count; ++i)
      assert(length(device.intercept(i) - device.disks[i].center) <= device.disks[i].radius + .01);
    for (int fix = 0; fix < 10; ++fix) {
      Vec next{float(-12000 + fix * 20), float(-12000 + fix * 30)};
      device.updatePosition(next);
      desktop.update_position({next.x, next.y});
      for (int i = 0; i < 16; ++i) {
        assert(device.iterate());
        desktop.improve(1);
      }
      assert(std::abs(device.bestDistance() - desktop.best_distance()) < .1);
    }
    assert(device.advance());
    desktop.advance_waypoint();
    assert(device.iterate());
    desktop.improve(1);
    assert(std::abs(device.bestDistance() - desktop.best_distance()) < .1);
  }
  std::cout << "180 device/desktop comparisons, max static distance difference "
            << maximumDifference << " m; solver bytes " << sizeof(Solver<20>) << "\n";
}
int main() {
  timerTests();
  geometryTests();
  solverTests();
  auto& runtime = sim::runtime();
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/route-optimizer/state";
  options.cardRoot = "sim/build-tests/route-optimizer/card";
  runtime.configure(options);
  runtime.boot();
  settings.loadDefaults();
  settings.log_autoStart = false;
  settings.log_autoStop = false;
  settings.setDeveloperMode(false);
  sim::scenario().setBus(&bus);
  sim::freeFlight().start(sim::clock().millis(), &bus, 1500);
  auto run = [&](unsigned ms) {
    auto until = sim::clock().millis() + ms;
    while (sim::clock().millis() < until) runtime.step();
  };
  run(4000);
  // Reproduce permanent callback-count/wall-time misalignment seen on hardware.
  // Three physical alarms deliver one interrupt; old epoch-based gate starves.
  sim::clock().maskTimerInterrupts(true);
  sim::clock().advanceUs(30000);
  sim::clock().maskTimerInterrupts(false);
  route_experiment::Disk disks[40];
  const size_t n = route_experiment::reviewRoute("mixed7", disks);
  navigator.clear();
  navigator.totalRoutes = 1;
  auto& route = navigator.routes[1];
  route = Route{};
  route.setName("Device mixed7");
  for (size_t i = 0; i < n; ++i) {
    Waypoint point;
    point.setName("TURN");
    point.setCoordinates(47.51 + disks[i].center.y / 111320.0,
                         11.18 + disks[i].center.x / (111320 * std::cos(toRadians(47.51))));
    point.ele = 800;
    assert(navigator.addWaypoint(point));
    assert(navigator.addRoutePoint(&route, WaypointID(i + 1), disks[i].radius));
  }
  sim::freeFlight().relocate(-700, 0, 90);
  run(1200);
  assert(navigator.activateRoute(RouteID(1), false));
  const double original = navigator.goalDistanceRemaining();
  assert(!settings.dev_mode);
  settings.disp_showNavPage = true;
  settings.disp_navFields[0] = NavField::DistanceToCylinder;
  settings.disp_navFields[1] = NavField::DistanceToGoal;
  settings.disp_navFields[2] = NavField::GlideToCylinder;
  settings.disp_navFields[3] = NavField::GlideToGoal;
  display.setPage(MainPage::Navigate);
  run(6500);
  const auto* solved = route_guidance::solution(navigator);
  assert(solved);
  assert(solved->goalM < original && solved->goalM > 5000 && solved->goalM < 7000);
  assert(solved->hasFollowing);
  assert(route_guidance::diagnostics().publications == 0);
  assert(route_guidance::diagnostics().iterations == 0);
  assert(route_guidance::diagnostics().maxSliceUs == 0);
  assert(!settings.dev_mode);
  settings.setDeveloperMode(true);  // Enables measurement only.
  assert(route_guidance::solution(navigator));
  const auto publicationsBeforeMask = route_guidance::diagnostics().publications;
  sim::clock().maskTimerInterrupts(true);
  sim::clock().advanceUs(30000);
  sim::clock().maskTimerInterrupts(false);
  run(2500);
  assert(route_guidance::diagnostics().publications > publicationsBeforeMask);
  assert(solved->marginM > 15 && solved->marginM <= 20);
  assert(std::abs(navigator.cylinderDistanceRemaining() - solved->cylinderM) < .001);
  const Geo pilot{gps.location.lat(), gps.location.lng()};
  const Geo crossing = destination(pilot, solved->bearingDeg, solved->cylinderM);
  assert(std::abs(distance(crossing,
                           {navigator.activePoint.latitude(), navigator.activePoint.longitude()}) -
                  300) < .01);
  assert(solved->cylinderM > navigator.pointDistanceRemaining - 300 + 20);
  assert(std::abs(navigator.glideToCylinder() - solved->cylinderM / (gps.altitude.meters() - 800)) <
         .001);
  assert(std::abs(navigator.glideToGoal() - solved->goalM / (gps.altitude.meters() - 800)) < .001);
  double following = 0;
  assert(navigator.followingTurn(following));
  // Repeated navigation reads of one position cannot enqueue more solver work.
  for (int i = 0; i < 300; ++i) route_guidance::service(navigator);
  const auto iterations = route_guidance::diagnostics().iterations;
  for (int i = 0; i < 10; ++i) navigator.update();
  for (int i = 0; i < 100; ++i) route_guidance::service(navigator);
  assert(route_guidance::diagnostics().iterations == iterations);
  // A newly received GGA for the same UTC epoch must not grant another budget.
  const auto fixes = route_guidance::diagnostics().fixes;
  const double lat = gps.location.lat(), lon = gps.location.lng();
  const double nmeaLat = int(lat) * 100 + (lat - int(lat)) * 60;
  const double nmeaLon = int(lon) * 100 + (lon - int(lon)) * 60;
  char body[160], sentence[180];
  snprintf(body, sizeof(body),
           "GNGGA,%02d%02d%02d.%02d,%011.6f,N,%012.6f,E,1,12,0.8,1500.0,M,0.0,M,,", gps.time.hour(),
           gps.time.minute(), gps.time.second(), gps.time.centisecond(), nmeaLat, nmeaLon);
  unsigned checksum = 0;
  for (const char* c = body; *c; ++c) checksum ^= *c;
  snprintf(sentence, sizeof(sentence), "$%s*%02X", body, checksum);
  sim::clock().advanceUs(100000);
  gps.on_receive(GpsMessage(NMEAString(sentence)));
  gps.update();
  for (int i = 0; i < 100; ++i) route_guidance::service(navigator);
  assert(route_guidance::diagnostics().fixes == fixes);
  assert(route_guidance::diagnostics().iterations == iterations);
  std::ofstream("sim/build-tests/route-optimizer/device-navigation.png", std::ios::binary)
      << runtime.screenshotPng(3);
  // Developer mode changes collection, not guidance or its current plan.
  const auto measuredPublications = route_guidance::diagnostics().publications;
  settings.setDeveloperMode(false);
  run(2500);
  assert(route_guidance::solution(navigator));
  assert(route_guidance::diagnostics().publications == measuredPublications);
  settings.setDeveloperMode(true);
  // Same-size route edits invalidate the complete plan; no stale cached target.
  navigator.routes[1].goalType = RouteGoalType::Line;
  navigator.update();
  assert(!route_guidance::solution(navigator));
  assert(std::string(route_guidance::status()) == "unsupported_task");
  navigator.routes[1].goalType = RouteGoalType::Cylinder;
  navigator.update();
  run(3500);
  assert(route_guidance::solution(navigator));
  assert(!flightTimer_isRunning());
  // With the timer stopped the old leg remains active, so its boundary zero
  // is observable. Move only a few meters past the real entry, short of the aim.
  const auto& plan = *route_guidance::solution(navigator);
  const Geo from{gps.location.lat(), gps.location.lng()};
  const Geo inside = destination(from, plan.bearingDeg, plan.cylinderM + 5);
  assert(distance(inside, {plan.targetLatitude, plan.targetLongitude}) > 5);
  sim::freeFlight().relocate((inside.lon - 11.18) * 111320 * std::cos(toRadians(47.51)),
                             (inside.lat - 47.51) * 111320, plan.bearingDeg);
  run(1400);
  assert(navigator.activeRoutePointIndex == 1);
  assert(route_guidance::solution(navigator));
  assert(navigator.cylinderDistanceRemaining() == 0);
  flightTimer_start();
  run(1400);
  assert(navigator.activeRoutePointIndex == 2);
  assert(navigator.cylinderDistanceRemaining() > 0);
  run(2000);
  assert(route_guidance::solution(navigator));
  assert(navigator.activateRoute(RouteID(1), RouteIndex(7), false));
  run(2200);
  solved = route_guidance::solution(navigator);
  assert(solved && !solved->hasFollowing);
  assert(!navigator.followingTurn(following));
  assert(std::abs(solved->goalM - solved->cylinderM) < .001);
  // Completion and cancellation cannot expose old targets.
  sim::freeFlight().relocate(83.346, -1410, 90);
  run(1400);
  assert(navigator.routeCompleted() && !route_guidance::solution(navigator));
  assert(navigator.cylinderDistanceRemaining() == 0);
  navigator.cancelNav();
  assert(!route_guidance::solution(navigator));
  // Exercise the full existing route limit through the actual task manager.
  sim::freeFlight().relocate(-700, 0, 90);
  run(1400);
  navigator.clear();
  navigator.totalRoutes = 1;
  navigator.routes[1] = Route{};
  navigator.routes[1].setName("Twenty points");
  for (int i = 0; i < 20; ++i) {
    Waypoint p;
    p.setName("STRESS");
    p.setCoordinates(47.51 + (i % 2 ? 300 : -300) / 111320.0,
                     11.18 + (500 + i * 800) / (111320 * std::cos(toRadians(47.51))));
    p.ele = 800;
    assert(navigator.addWaypoint(p));
    assert(navigator.addRoutePoint(&navigator.routes[1], WaypointID(i + 1), 150));
  }
  JsonDocument capacityRoute;
  capacityRoute["name"] = "Capacity boundary";
  auto capacityPoints = capacityRoute["points"].to<JsonArray>();
  for (int i = 1; i <= 20; ++i) {
    const auto& p = navigator.routePoint(RouteID(1), RouteIndex(i));
    auto row = capacityPoints.add<JsonObject>();
    row["name"] = "STRESS";
    row["lat"] = p.latitude();
    row["lon"] = p.longitude();
    row["alt_m"] = 800;
    row["radius_m"] = 150;
  }
  route_store::ImportResult saved;
  assert(route_store::saveRouteJson(capacityRoute, false, saved));
  assert(saved.points == 20 && route_store::loadRouteFile(saved.path, false));
  auto extra = capacityPoints.add<JsonObject>();
  extra["lat"] = 47.51;
  extra["lon"] = 11.18;
  assert(!route_store::saveRouteJson(capacityRoute, false, saved));
  assert(saved.error == "Route exceeds the 20-point limit.");
  capacityRoute["schema"] = "leaf.route";
  auto oversized = SD_MMC.open("/routes/oversize.json", FILE_WRITE);
  assert(oversized && serializeJson(capacityRoute, oversized));
  oversized.close();
  NavLoadError loadError;
  assert(!route_store::loadRouteFile("/routes/oversize.json", true, &loadError));
  assert(loadError == NavLoadError::RoutePointLimit);
  assert(navigator.routes[1].totalPoints == 20);
  assert(!navigator.addRoutePoint(&navigator.routes[1], WaypointID(1), 150));
  assert(navigator.routes[1].totalPoints == 20);
  assert(navigator.activateRoute(RouteID(1), false));
  run(12000);
  solved = route_guidance::solution(navigator);
  assert(solved && solved->hasFollowing && solved->goalM > 10000);
  assert(route_guidance::diagnostics().workspaceBytes <= 1536);
  // Stale GPS invalidates both metrics and follow-on guidance, then recovers.
  sim::freeFlight().stop();
  run(16000);
  assert(!navigator.hasNavSolution() && !route_guidance::solution(navigator));
  assert(!navigator.followingTurn(following));
  sim::freeFlight().start(sim::clock().millis(), &bus, 1500);
  sim::freeFlight().relocate(-700, 0, 90);
  run(8000);
  assert(route_guidance::solution(navigator));
  class Output : public Print {
   public:
    std::string text;
    size_t write(uint8_t c) override {
      text += char(c);
      return 1;
    }
  } output;
  route_guidance::writeJson(output);
  JsonDocument diagnostics;
  assert(!deserializeJson(diagnostics, output.text));
  assert(diagnostics["valid"].as<bool>() && diagnostics["workspace_bytes"].as<int>() <= 1536);
  assert(diagnostics["route_capacity"].as<int>() == 20);
  assert(diagnostics["remaining_points"].as<int>() == 20);
  assert(diagnostics["max_first_plan_latency_ms"].as<unsigned>() > 0);
  assert(diagnostics["max_request_latency_ms"].as<unsigned>() > 0);
  assert(diagnostics["max_request_latency_ms"].as<unsigned>() >=
         diagnostics["last_request_latency_ms"].as<unsigned>());
  assert(diagnostics["plan_source_age_ms"].as<unsigned>() < 3000);
  assert(diagnostics["slices_over_budget"].as<unsigned>() == 0);  // Virtual clock only.
  assert(diagnostics["max_geometry_us"].is<unsigned>());
  assert(diagnostics["max_publish_point_us"].is<unsigned>());
  std::ofstream("sim/build-tests/route-optimizer/diagnostics.json") << output.text;
  navigator.cancelNav();
  for (unsigned n : {20u, 21u}) {
    auto gpx = SD_MMC.open("/capacity.gpx", FILE_WRITE);
    assert(gpx);
    gpx.print("<gpx><rte><name>Capacity</name>");
    for (unsigned i = 0; i < n; ++i)
      gpx.printf("<rtept lat=\"47.51\" lon=\"11.18\"><name>P%u</name></rtept>", i);
    gpx.print("</rte></gpx>");
    gpx.close();
    assert(nav_readFile(SD_MMC, "/capacity.gpx", &loadError) == (n == 20));
    assert(loadError == (n == 20 ? NavLoadError::None : NavLoadError::RoutePointLimit));
    assert(navigator.totalRoutePointRefs == (n == 20 ? 20 : 0));
    assert(navigator.totalRoutes == (n == 20 ? 1 : 0));
  }
  std::cout << "Firmware guidance checks passed; workspace "
            << route_guidance::diagnostics().workspaceBytes << " bytes\n";
}
