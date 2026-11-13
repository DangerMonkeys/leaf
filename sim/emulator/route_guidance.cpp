// Opt-in desktop LCD/map adapter. Compiled only by sim/Makefile.
#include "route_guidance.h"
#include <deque>
#include <mutex>
#include <sstream>
#include <vector>
#include "../route_solver/guidance.h"
#include "free_flight.h"
#include "instruments/gps.h"
#include "logging/log.h"
#include "navigation/gpx.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/clock.h"
#include "ui/display/display.h"
#include "ui/settings/settings.h"
using namespace route_experiment;
namespace sim {
  namespace {
    constexpr double radians = 3.14159265358979323846 / 180, latitude = 47.51, longitude = 11.18;
    const double longitudeScale = 111320 * std::cos(latitude * radians);
    std::mutex snapshotMutex;
    std::string cached = "{\"enabled\":false}";
    std::deque<std::string> telemetry;
    unsigned lastLoggedUpdate = 0;
    bool enabled = false, initialized = false, valid = false;
    std::string presetName = "tangent6";
    MarginPolicy policy = MarginPolicy::Grazing30;
    std::array<Disk, 40> geometry;
    std::vector<std::vector<Vec>> outlines;
    size_t count = 0, active = 0;
    PrimalDual<40> solver, startSolver;
    GuidancePlan<40> plan;
    constexpr Vec start{-700, 0};
    Vec pilot{}, entry{};
    double distance = 0, course = 0, cylinderDistance = 0, legInitialDistance = 0;
    double followingCourse = 0, startIdeal = 0, startBuffered = 0;
    bool following = false;
    size_t entryLeg = 40;
    uint32_t lastFix = 0;
    unsigned updates = 0;
    uint32_t trialStarted = 0;
    Vec local(double lat, double lon) {
      return {float((lon - longitude) * longitudeScale), float((lat - latitude) * 111320)};
    }
    double nativeDistance(Vec a, Vec b) {
      return gps.distanceBetween(latitude + a.y / 111320, longitude + a.x / longitudeScale,
                                 latitude + b.y / 111320, longitude + b.x / longitudeScale);
    }
    void compareStart() {
      startIdeal =
          guidancePlan<40>(startSolver, geometry.data(), start, MarginPolicy::None).distance;
      startBuffered = guidancePlan<40>(startSolver, geometry.data(), start, policy).distance;
    }
    bool snapshotMatchesRoute() {
      return enabled && valid && navigator.hasNavSolution() &&
             navigator.activeRouteIndex == RouteID(1) &&
             int16_t(navigator.activeRoutePointIndex) == int(active + 1);
    }
    // Desktop reference calculation: solve the crossing using the SAME geographic
    // distance test as the native sequencer, along the line to the guidance target.
    // This bisection is assessment infrastructure, not the proposed embedded kernel.
    void findEntry(const Navigator& nav) {
      const Vec center = local(nav.activePoint.latitude(), nav.activePoint.longitude());
      const double radius = nav.activeCylinderRadiusM();
      auto& target = plan.points[0];
      const double targetRadius = nativeDistance(target, center);
      if (targetRadius > radius) {
        target = center + (target - center) * float(std::max(0.0, radius - .05) / targetRadius);
      }
      // A second check handles projection/float degeneracy conservatively.
      if (nativeDistance(target, center) > radius) target = center;
      if (nativeDistance(pilot, center) < radius) {
        entry = pilot;
        cylinderDistance = 0;
        return;
      }
      double lo = 0, hi = 1;
      for (unsigned i = 0; i < 28; ++i) {
        const double mid = (lo + hi) * .5;
        const Vec q = pilot + (target - pilot) * float(mid);
        if (nativeDistance(q, center) < radius)
          hi = mid;
        else
          lo = mid;
      }
      entry = pilot + (target - pilot) * float(hi);
      cylinderDistance = nativeDistance(pilot, entry);
    }
    void publish(bool complete = false) {
      std::ostringstream out;
      out.setf(std::ios::fixed);
      out.precision(3);
      out << "{\"enabled\":" << (enabled ? "true" : "false")
          << ",\"valid\":" << (valid ? "true" : "false") << ",\"preset\":\"" << presetName
          << "\",\"policy\":\"" << policyName(policy) << "\",\"active\":" << active
          << ",\"completed\":" << (complete ? "true" : "false") << ",\"remainingM\":" << distance
          << ",\"startIdealM\":" << startIdeal << ",\"startPolicyM\":" << startBuffered
          << ",\"startExtraPct\":"
          << (startIdeal > .001 ? 100 * (startBuffered / startIdeal - 1) : 0)
          << ",\"cylinderRemainingM\":" << cylinderDistance << ",\"entry\":[" << entry.x << ','
          << entry.y << ']' << ",\"hasNext\":" << (following ? "true" : "false")
          << ",\"nextBearingDeg\":" << followingCourse
          << ",\"nextTurnDeg\":" << wrapDegrees(followingCourse - gps.course.deg())
          << ",\"bearingDeg\":" << course
          << ",\"turnDeg\":" << wrapDegrees(course - gps.course.deg()) << ",\"updates\":" << updates
          << ",\"gps\":[" << pilot.x << ',' << pilot.y << "],\"cylinders\":[";
      for (size_t i = 0; i < count; ++i) {
        if (i) out << ',';
        out << "[" << geometry[i].center.x << ',' << geometry[i].center.y << ','
            << geometry[i].radius << "]";
      }
      out << "],\"outlines\":[";
      for (size_t i = 0; i < outlines.size(); ++i) {
        if (i) out << ',';
        out << '[';
        for (size_t j = 0; j < outlines[i].size(); ++j) {
          if (j) out << ',';
          out << '[' << outlines[i][j].x << ',' << outlines[i][j].y << ']';
        }
        out << ']';
      }
      out << "],\"path\":[";
      if (valid)
        for (size_t i = 0; i < plan.count; ++i) {
          if (i) out << ',';
          out << "[" << plan.points[i].x << ',' << plan.points[i].y << ',' << plan.margins[i]
              << "]";
        }
      out << "]}";
      std::lock_guard<std::mutex> lock(snapshotMutex);
      cached = out.str();
      if (valid && updates != lastLoggedUpdate) {
        lastLoggedUpdate = updates;
        std::ostringstream row;
        row.setf(std::ios::fixed);
        row.precision(3);
        row << (clock().millis() - trialStarted) / 1000.0 << ',' << presetName << ','
            << policyName(policy) << ',' << active << ',' << pilot.x << ',' << pilot.y << ','
            << course << ',' << wrapDegrees(course - gps.course.deg()) << ',' << plan.points[0].x
            << ',' << plan.points[0].y << ',' << distance << ',' << plan.margins[0] << ','
            << cylinderDistance << ',' << entry.x << ',' << entry.y << ',' << following << ','
            << followingCourse;
        telemetry.push_back(row.str());
        if (telemetry.size() > 3600) telemetry.pop_front();
      }
    }
  }  // namespace
  bool routeGuidanceValid(const std::string& preset, const std::string& name) {
    Disk disks[40];
    MarginPolicy choice;
    return (preset.empty() || reviewRoute(preset.c_str(), disks) > 0) &&
           parsePolicy(name.c_str(), choice);
  }
  bool configureRouteGuidance(const std::string& preset, const std::string& name) {
    if (!routeGuidanceValid(preset, name)) return false;
    parsePolicy(name.c_str(), policy);
    if (preset.empty()) {
      // Policy changes reuse the converged nominal path; only its buffer changes.
      valid = false;
      if (enabled) compareStart();
      publish();
      return true;
    }
    enabled = false;
    initialized = false;
    valid = false;
    updates = 0;
    active = 0;
    distance = 0;
    entryLeg = 40;
    following = false;
    if (flightTimer_isRunning()) flightTimer_stop(false);
    {
      std::lock_guard<std::mutex> lock(snapshotMutex);
      telemetry.clear();
      lastLoggedUpdate = 0;
    }
    presetName = preset;
    count = reviewRoute(preset.c_str(), geometry.data());
    startSolver.initialize(geometry.data(), count, start);
    startSolver.improve(512);
    compareStart();
    scenario().pause();
    freeFlight().start(clock().millis(), scenario().injectorBus(), 1500);
    freeFlight().relocate(-700, 0, 90);
    navigator.clear();
    navigator.totalRoutes = 1;
    auto& route = navigator.routes[1];
    // Navigator::clear resets counts but intentionally leaves array storage intact.
    route = Route{};
    route.setName("Guidance trial");
    for (size_t i = 0; i < count; ++i) {
      Waypoint waypoint;
      waypoint.setName(("TP" + std::to_string(i + 1)).c_str());
      waypoint.setCoordinates(latitude + geometry[i].center.y / 111320,
                              longitude + geometry[i].center.x / longitudeScale);
      waypoint.ele = 500;
      const auto id = navigator.addOrFindWaypoint(waypoint);
      if (!navigator.addRoutePoint(&route, id, uint16_t(geometry[i].radius))) return false;
    }
    // Render the actual TinyGPSPlus spherical achievement boundary on the
    // simulator's injection plane rather than assuming that plane is geodesic.
    outlines.clear();
    for (size_t i = 0; i < count; ++i) {
      const auto& waypoint = navigator.routePoint(RouteID(1), RouteIndex(i + 1));
      double lat1 = waypoint.latitude() * radians, lon1 = waypoint.longitude() * radians;
      double arc = geometry[i].radius / 6372795.0;
      std::vector<Vec> ring;
      for (unsigned j = 0; j <= 144; ++j) {
        double theta = j * 2.5 * radians;
        double lat2 = std::asin(std::sin(lat1) * std::cos(arc) +
                                std::cos(lat1) * std::sin(arc) * std::cos(theta));
        double lon2 = lon1 + std::atan2(std::sin(theta) * std::sin(arc) * std::cos(lat1),
                                        std::cos(arc) - std::sin(lat1) * std::sin(lat2));
        ring.push_back(local(lat2 / radians, lon2 / radians));
      }
      outlines.push_back(std::move(ring));
    }
    settings.disp_showNavPage = true;
    // Keep the LCD on Navigate; thermal auto-switch could otherwise hide guidance.
    settings.disp_showThermalCorePage = false;
    settings.disp_showThermalTrackPage = false;
    navigator.activateRoute(RouteID(1), RouteIndex(1), false);
    trialStarted = clock().millis();
    enabled = true;
    display.setPage(MainPage::Navigate);
    publish();
    return true;
  }
  bool routeGuidanceExperimentActive() { return enabled; }

  void disableRouteGuidance() {
    enabled = false;
    valid = false;
    initialized = false;
    std::lock_guard<std::mutex> lock(snapshotMutex);
    cached = "{\"enabled\":false}";
  }
  bool updateRouteGuidance(Navigator& nav, double& outputBearing) {
    if (!enabled) return false;
    if (!nav.activeRouteIndex || nav.activeRouteIndex != RouteID(1) ||
        nav.routes[1].totalPoints != count) {
      valid = false;
      publish();
      return false;
    }
    // Refuse to apply a trial's targets to another route loaded through the LCD.
    for (size_t i = 0; i < count; ++i) {
      const auto& point = nav.routePoint(RouteID(1), RouteIndex(i + 1));
      const auto actual = local(point.latitude(), point.longitude());
      if (std::hypot(actual.x - geometry[i].center.x, actual.y - geometry[i].center.y) > .1 ||
          nav.routePointMeta(RouteID(1), RouteIndex(i + 1)).radiusM != geometry[i].radius) {
        valid = false;
        publish();
        return false;
      }
    }
    // Everything except JSON observation executes on the firmware/device thread.
    GPSPositionSnapshot fix;
    if (!gps.lastValidFix(fix) || int32_t(fix.capturedAtMs - trialStarted) < 0) return false;
    if (!flightTimer_isRunning()) flightTimer_start();
    const size_t next = size_t(int16_t(nav.activeRoutePointIndex) - 1);
    if (next >= count) return false;
    pilot = local(fix.latitude, fix.longitude);
    if (!initialized || next < active) {
      active = next;
      solver.initialize(geometry.data() + active, count - active, pilot);
      solver.improve(64);
      initialized = true;
      lastFix = fix.capturedAtMs;
      ++updates;
    } else {
      while (active < next) {
        solver.advance_waypoint();
        ++active;
      }
      solver.update_position(pilot);
      if (fix.capturedAtMs != lastFix) {
        solver.improve(16);
        lastFix = fix.capturedAtMs;
        ++updates;
      }
    }
    plan = guidancePlan<40>(solver, geometry.data() + active, pilot, policy);
    findEntry(nav);
    if (entryLeg != active) {
      entryLeg = active;
      legInitialDistance = cylinderDistance;
    }
    // Reprice after any sub-metre correction onto the actual geographic cylinder.
    plan.distance = 0;
    Vec previous = pilot;
    for (size_t i = 0; i < plan.count; ++i) {
      const Vec endpoint = i + 1 == plan.count
                               ? cylinderEntry(previous, plan.points[i], geometry[active + i])
                               : plan.points[i];
      plan.distance += std::hypot(endpoint.x - previous.x, endpoint.y - previous.y);
      previous = plan.points[i];
    }
    following = plan.count > 1;
    followingCourse = following ? bearing(pilot, plan.points[1]) : 0;
    if (followingCourse < 0) followingCourse += 360;
    valid = true;
    const bool complete =
        active + 1 == count &&
        gps.distanceBetween(fix.latitude, fix.longitude, nav.activePoint.latitude(),
                            nav.activePoint.longitude()) < nav.activeCylinderRadiusM();
    distance = complete ? 0 : plan.count == 1 ? cylinderDistance : plan.distance;
    const Vec delta = plan.points[0] - pilot;
    course =
        complete || dot(delta, delta) < .0025 ? gps.course.deg() : bearing(pilot, plan.points[0]);
    if (course < 0) course += 360;
    outputBearing = course;
    publish(complete);
    return true;
  }
  void serviceRouteGuidance() {
    if (enabled && valid && !navigator.hasActivePoint())
      disableRouteGuidance();
    else if (enabled && valid && !gps.hasUsableFix()) {
      valid = false;
      publish();
    }
  }
  bool routeGuidanceDistance(double& value) {
    if (!enabled || !valid || navigator.activeRouteIndex != RouteID(1) ||
        int16_t(navigator.activeRoutePointIndex) != int(active + 1))
      return false;
    value = distance;
    return true;
  }
  bool routeGuidanceCylinderDistance(double& value, double* initialLeg) {
    if (!snapshotMatchesRoute()) return false;
    value = cylinderDistance;
    if (initialLeg) *initialLeg = legInitialDistance;
    return true;
  }
  bool routeGuidanceFollowingTurn(double& turn) {
    if (!snapshotMatchesRoute() || !following) return false;
    turn = wrapDegrees(followingCourse - gps.course.deg());
    return true;
  }
  std::string routeGuidanceLog() {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    std::string out =
        "seconds,preset,policy,active,gps_east_m,gps_north_m,bearing_deg,turn_deg,target_east_m,"
        "target_north_m,remaining_m,margin_m,cylinder_remaining_m,entry_east_m,entry_north_m,has_"
        "next,next_bearing_deg\n";
    for (const auto& row : telemetry) out += row + "\n";
    return out;
  }
  std::string routeGuidanceJson() {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    return cached;
  }
}  // namespace sim
