#include "route_guidance.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include "dispatch/message_bus.h"
#include "free_flight.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logging/log.h"
#include "navigation/gpx.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/display.h"
#include "ui/display/pages/primary/page_navigate.h"
extern MessageBus<11> bus;
int main() {
  auto& device = sim::runtime();
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/route-guidance/state";
  options.cardRoot = "sim/build-tests/route-guidance/card";
  device.configure(options);
  device.boot();
  sim::scenario().setBus(&bus);
  sim::freeFlight().start(sim::clock().millis(), &bus, 1500);
  auto run = [&](unsigned milliseconds) {
    auto until = sim::clock().millis() + milliseconds;
    while (sim::clock().millis() < until) device.step();
  };
  run(4000);
  assert(sim::configureRouteGuidance("tangent6", "angle5_50"));
  run(22000);
  assert(gps.hasUsableFix());
  assert(navigator.activeRoutePointIndex == 1);
  assert(display.getPage() == MainPage::Navigate);
  JsonDocument state;
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  assert(state["valid"].as<bool>());
  // Solid map boundaries match the exact spherical radius used by the sequencer.
  for (auto vertex : state["outlines"][0].as<JsonArray>()) {
    double lat = 47.51 + vertex[1].as<double>() / 111320;
    double lon =
        11.18 + vertex[0].as<double>() / (111320 * std::cos(47.51 * 3.141592653589793 / 180));
    assert(std::abs(gps.distanceBetween(lat, lon, navigator.activePoint.latitude(),
                                        navigator.activePoint.longitude()) -
                    400) < .04);
  }
  assert(flightTimer_isLogging());
  const double startIdeal = state["startIdealM"].as<double>();
  const double startPolicy = state["startPolicyM"].as<double>();
  assert(startPolicy > startIdeal && startIdeal > 1000);
  assert(std::abs(state["startExtraPct"].as<double>() - 100 * (startPolicy / startIdeal - 1)) <
         .002);
  const double interceptDistance = state["cylinderRemainingM"].as<double>();
  const double entryLat = 47.51 + state["entry"][1].as<double>() / 111320;
  const double entryLon =
      11.18 + state["entry"][0].as<double>() / (111320 * std::cos(47.51 * 3.141592653589793 / 180));
  assert(std::abs(gps.distanceBetween(entryLat, entryLon, navigator.activePoint.latitude(),
                                      navigator.activePoint.longitude()) -
                  400) < .04);
  assert(std::abs(navigator.cylinderDistanceRemaining() - interceptDistance) < .02);
  assert(interceptDistance > navigator.pointDistanceRemaining - 400 + 40);
  assert(navigator.cylinderTimeRemaining() ==
         uint32_t(navigator.cylinderDistanceRemaining() / gps.speed.mps()));
  assert(std::abs(navigator.glideToCylinder() -
                  navigator.cylinderDistanceRemaining() /
                      (gps.altitude.meters() - navigator.activePoint.ele)) < .001);
  assert(baro.climbRateFilteredValid());
  gps.setGlideAverageSeconds(0);
  gps.update();
  assert(gps.getGlideRatio() > 0);
  assert(std::abs(navigator.arrivalAltitudeAboveCylinder() -
                  (gps.altitude.meters() - navigator.activePoint.ele -
                   navigator.cylinderDistanceRemaining() / gps.getGlideRatio())) < .02);
  double followTurn = 0;
  assert(sim::routeGuidanceFollowingTurn(followTurn));
  assert(std::abs(followTurn - state["nextTurnDeg"].as<double>()) < .02);
  const double dx = state["path"][1][0].as<double>() - state["gps"][0].as<double>();
  const double dy = state["path"][1][1].as<double>() - state["gps"][1].as<double>();
  assert(std::abs(std::remainder(
             state["nextBearingDeg"].as<double>() - std::atan2(dx, dy) * 180 / 3.141592653589793,
             360)) < .02);
  assert(std::abs(navigator.goalDistanceRemaining() - state["remainingM"].as<double>()) < .02);
  assert(std::abs(navigator.turnToActive - state["turnDeg"].as<double>()) < .02);
  double center = gps.courseTo(gps.location.lat(), gps.location.lng(),
                               navigator.activePoint.latitude(), navigator.activePoint.longitude());
  assert(std::abs(std::remainder(state["bearingDeg"].as<double>() - center, 360)) > 10);
  std::ofstream("sim/build-tests/route-guidance/navigate.png", std::ios::binary)
      << device.screenshotPng(3);
  // Near the target but outside the real cylinder: no synthetic-target sequencing.
  sim::freeFlight().relocate(0, -30, 0);
  run(1400);
  assert(navigator.activeRoutePointIndex == 1);
  // Entry into the real cylinder sequences independently of optimized intercept.
  sim::freeFlight().relocate(0, 40, 0);
  run(1400);
  assert(navigator.activeRoutePointIndex == 2);
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  assert(state["startIdealM"].as<double>() == startIdeal &&
         state["startPolicyM"].as<double>() == startPolicy);
  // Final active point has no following marker, even before entering goal.
  assert(navigator.activateRoute(RouteID(1), RouteIndex(6), false));
  sim::freeFlight().relocate(4060, 0, 90);
  run(1400);
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  assert(!state["hasNext"].as<bool>() && !sim::routeGuidanceFollowingTurn(followTurn));
  assert(navigator.cylinderDistanceRemaining() > 0);
  sim::freeFlight().relocate(4200, 0, 90);
  run(1800);
  assert(navigator.cylinderDistanceRemaining() == 0);
  std::ofstream("sim/build-tests/route-guidance/goal.png", std::ios::binary)
      << device.screenshotPng(3);
  // Restart with a fresh origin while old GPS fix lies inside TP1.
  assert(sim::configureRouteGuidance("tangent6", "fixed50"));
  run(1600);
  assert(navigator.activeRoutePointIndex == 1);
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  navigator.update();  // Consume any fix that arrived after the scheduled nav slot.
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  assert(state["valid"].as<bool>());
  assert(navigator.routes[1].totalPoints == 6);
  const auto updateCount = state["updates"].as<unsigned>();
  assert(sim::configureRouteGuidance("", "angle0_50"));
  navigator.update();
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  assert(state["valid"].as<bool>() && state["updates"].as<unsigned>() == updateCount);
  assert(state["policy"].as<std::string>() == "angle0_50");
  assert(std::abs(state["startIdealM"].as<double>() - state["startPolicyM"].as<double>()) > .01);
  assert(sim::configureRouteGuidance("", "none"));
  navigator.update();
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  assert(state["startIdealM"].as<double>() == state["startPolicyM"].as<double>());
  assert(state["startExtraPct"].as<double>() == 0);
  const auto trace = sim::routeGuidanceLog();
  assert(trace.find("seconds,preset,policy,active,") == 0);
  assert(trace.find("tangent6,fixed50,") != std::string::npos);
  sim::disableRouteGuidance();
  navigator.update();
  center = gps.courseTo(gps.location.lat(), gps.location.lng(), navigator.activePoint.latitude(),
                        navigator.activePoint.longitude());
  assert(std::abs(std::remainder(navigator.turnToActive - (center - gps.course.deg()), 360)) <
         .001);
  assert(sim::configureRouteGuidance("tangent6", "angle0_50"));
  run(1600);
  navigator.clear();
  run(100);
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  assert(!state["enabled"].as<bool>());
  assert(sim::configureRouteGuidance("mixed7", "angle0_50"));
  run(2200);
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  assert(state["valid"].as<bool>() && state["preset"].as<std::string>() == "mixed7");
  assert(state["cylinders"].size() == 7 && state["path"].size() == 7);
  assert(navigator.routes[1].totalPoints == 7);
  assert(sim::configureRouteGuidance("", "grazing30"));
  run(1000);
  assert(!deserializeJson(state, sim::routeGuidanceJson()));
  assert(state["valid"].as<bool>() && state["policy"].as<std::string>() == "grazing30");
  assert(!sim::routeGuidanceValid("bad", "angle5_50"));
  assert(!sim::routeGuidanceValid("tangent6", "bad"));
  // Match the supplied silhouette with the user-requested two-pixel tip trim,
  // treating the reference's red dynamic pixels as black LCD pixels. Disable only the experiment so
  // the secondary pointer does not contaminate the two-pointer reference region.
  sim::disableRouteGuidance();
  assert(navigator.hasNavSolution());
  navigatePage_reset();
  sim::Frame straight, pointingLeft;
  for (int angle : {0, 45, 90, 180, -45, -90}) {
    navigator.turnToActive = angle;
    navigatePage_draw();
    const auto frame = sim::captureFrame();
    if (angle == 0) straight = frame;
    if (angle == -90) pointingLeft = frame;
    std::ofstream out("sim/build-tests/route-guidance/pointers-" + std::to_string(angle) + ".png",
                      std::ios::binary);
    const auto png = sim::encodePng(frame, 4);
    out.write(png.data(), png.size());
  }
  std::ifstream expected("sim/tests/fixtures/nav-pointers-flat-mask.txt");
  assert(expected.good());
  int cropX, cropY, cropW, cropH;
  expected >> cropX >> cropY >> cropW >> cropH;
  unsigned mismatches = 0;
  for (int y = 0; y < cropH; ++y) {
    std::string row;
    expected >> row;
    assert(int(row.size()) == cropW);
    for (int x = 0; x < cropW; ++x) {
      const bool actual = straight.pixels[(cropY + y) * straight.width + cropX + x] != 0;
      if (actual != (row[x] == '#')) {
        std::cerr << "Pointer mismatch at " << cropX + x << ',' << cropY + y << " expected "
                  << row[x] << " got " << actual << '\n';
        ++mismatches;
      }
    }
  }
  std::cout << "Pointer reference crop: " << cropW * cropH << " pixels, " << mismatches
            << " mismatches\n";
  assert(mismatches == 0);
  // A left-pointing active arrow must not touch or erase the vario bar.
  navigator.cancelNav();
  navigatePage_draw();
  const auto noActiveArrow = sim::captureFrame();
  for (int y = 42; y <= 62; ++y)
    for (int x = 0; x < 18; ++x)
      assert(pointingLeft.pixels[y * pointingLeft.width + x] ==
             noActiveArrow.pixels[y * noActiveArrow.width + x]);
  std::cout << "Left-pointing pointer leaves the vario bar unchanged\n";
  std::cout << "Simulator LCD bearing/distance, actual-cylinder sequencing, reset and opt-out "
               "checks passed\n";
}
