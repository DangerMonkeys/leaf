// Documentation fixtures: synthetic sensors and routes, real firmware rendering.
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include "dispatch/message_bus.h"
#include "display_capture.h"
#include "free_flight.h"
#include "instruments/gps.h"
#include "logging/log.h"
#include "navigation/gpx.h"
#include "navigation/optimized_route_guidance.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/display.h"
#include "ui/display/pages/menu/page_menu_log.h"
#include "ui/display/pages/primary/page_navigate.h"
#include "ui/display/pages/primary/page_thermal_core.h"
#include "ui/display/pages/primary/page_thermal_track.h"
#include "ui/settings/settings.h"

extern MessageBus<11> bus;

int main() {
  static_assert(static_cast<unsigned>(NavField::Count) == 13,
                "Update the manual table and packaging when navigation fields change");
  auto& device = sim::runtime();
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  char fixture[] = "/tmp/leaf-manual-XXXXXX";
  assert(mkdtemp(fixture));
  options.statePath = std::string(fixture) + "/state";
  options.cardRoot = std::string(fixture) + "/card";
  device.configure(options);
  device.boot();
  settings.loadDefaults();
  settings.log_autoStart = settings.log_autoStop = false;
  settings.system_autoOff = 0;
  settings.disp_showThermalCorePage = settings.disp_showThermalTrackPage = true;
  settings.disp_showNavPage = true;
  settings.setDeveloperMode(false);
  sim::scenario().setBus(&bus);
  auto run = [&](unsigned ms) {
    const auto until = sim::clock().millis() + ms;
    while (sim::clock().millis() < until) device.step();
  };
  auto save = [&](const std::string& name) {
    std::ofstream("sim/build/manual-raw/" + name + ".png", std::ios::binary)
        << sim::encodePng(sim::captureFrame(), 1);
  };

  std::string error;
  assert(sim::scenario().load("sim/recordings/thermal-core.json", error));
  sim::scenario().play();
  run(20000);
  flightTimer_start();
  run(217000);
  thermalCorePage_draw();
  save("thermal-core");
  run(170000);
  thermalTrackPage_draw();
  save("thermal-track");
  sim::scenario().pause();

  sim::FreeFlightParameters parameters;
  parameters.windMps = 0;
  parameters.strengthMps = 0;
  parameters.sinkMps = 1;
  sim::freeFlight().configure(parameters);
  sim::freeFlight().start(sim::clock().millis(), &bus, 1500);
  sim::freeFlight().relocate(-900, -50, 90);
  run(20000);
  navigator.clear();
  navigator.totalRoutes = 1;
  auto& route = navigator.routes[1];
  route = Route{};
  route.setName("Valley route");
  const double east[] = {0, 1200, 2400};
  const double north[] = {0, 800, 0};
  const char* names[] = {"RIDGE", "VALLEY", "GOAL"};
  for (unsigned i = 0; i < 3; ++i) {
    Waypoint point;
    point.setName(names[i]);
    point.setCoordinates(47.51 + north[i] / 111320.0,
                         11.18 + east[i] / (111320 * std::cos(47.51 * 3.141592653589793 / 180)));
    point.ele = 800;
    assert(navigator.addWaypoint(point));
    assert(navigator.addRoutePoint(&route, WaypointID(i + 1), 400));
  }
  assert(navigator.activateRoute(RouteID(1), false));
  display.setPage(MainPage::Navigate);
  run(5000);
  assert(gps.hasUsableFix());
  assert(route_guidance::solution(navigator));
  navigatePage_reset();
  settings.disp_navFields[0] = NavField::DistanceToCylinder;
  settings.disp_navFields[1] = NavField::DistanceToGoal;
  settings.disp_navFields[2] = NavField::AltitudeAboveCylinder;
  settings.disp_navFields[3] = NavField::AltitudeAboveGoal;
  navigatePage_draw();
  save("navigate");
  // Every label/value crop comes from the same real top-left configurable field.
  for (unsigned i = 0; i < static_cast<unsigned>(NavField::Count); ++i) {
    settings.disp_navFields[0] = static_cast<NavField>(i);
    navigatePage_draw();
    save("nav-field-" + std::to_string(i));
  }
  flightTimer_stop(false);
  logMenuPage.draw();
  save("logging-menu");
}
