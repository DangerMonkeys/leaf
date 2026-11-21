#include <algorithm>
#include "diagnostics/heap_monitor.h"
#include "instruments/gps.h"
#include "navigation/gpx.h"
#include "navigation/optimized_route_guidance.h"
#include "navigation/route_optimizer.h"
#include "system/task_timer.h"
#include "ui/settings/settings.h"
#ifdef LEAF_SIM
#include "route_guidance.h"
#endif

namespace route_guidance {
  namespace {
    using namespace route_optimizer;
    enum class Phase : uint8_t { Idle, Geometry, Iterate, Publish };
    struct Published {
      Solution value;
      Geo aim{}, following{};
      double suffixM = 0;
      uint32_t sourceFixMs = 0;
    };
    struct Workspace {
      Solver<maxRoutePointRefs> solver;
      Projection projection;
      Published accepted{}, building{};
      GPSPositionSnapshot fix{};
      Vec pilot{}, previous{};
      Geo previousGeo{};
      Diagnostics counters{};
      uint32_t hash = 0, lastFix = 0, lastUtc = 0, buildStartedMs = 0, requestStartedMs = 0,
               restartStartedMs = 0;
      uint16_t remaining = 0;
      uint8_t route = 0, active = 0, count = 0, index = 0, sincePublish = 0;
      bool initialized = false, haveFix = false, haveUtc = false, firstLeg = true, blocked = false;
      bool requestPending = false;
      Phase phase = Phase::Idle;
      const char* state = "idle";
    };
    Workspace work;
    static_assert(sizeof(Workspace) <= 1536, "complete route-guidance workspace budget");
    Geo geo(const Waypoint& point) { return {point.latitude(), point.longitude()}; }
    const Waypoint& point(const Navigator& nav, size_t offset) {
      return nav.routePoint(nav.activeRouteIndex,
                            RouteIndex(int16_t(nav.activeRoutePointIndex) + offset));
    }
    const RoutePoint& meta(const Navigator& nav, size_t offset) {
      return nav.routePointMeta(nav.activeRouteIndex,
                                RouteIndex(int16_t(nav.activeRoutePointIndex) + offset));
    }
    bool eligible(const Navigator& nav) {
#ifdef LEAF_SIM
      if (sim::routeGuidanceExperimentActive()) return false;
#endif
      return nav.activeRouteIndex && nav.hasNavSolution() && !nav.routeCompleted();
    }
    uint32_t routeHash(const Navigator& nav) {
      const auto& r = nav.routes[nav.activeRouteIndex];
      uint32_t h = 2166136261u;
      const auto mix = [&](uint32_t value) { h = (h ^ value) * 16777619u; };
      mix(r.totalPoints);
      mix(uint8_t(r.goalType));
      mix(uint8_t(r.startType));
      mix(uint8_t(r.taskType));
      mix(uint8_t(r.earthModel));
      for (int i = 1; i <= r.totalPoints; ++i) {
        const auto& m = nav.routePointMeta(nav.activeRouteIndex, RouteIndex(i));
        const auto& p = nav.waypoint(m.waypointIndex);
        mix(p.latE7);
        mix(p.lonE7);
        mix(m.radiusM);
        mix(uint8_t(m.role));
      }
      return h;
    }
    void fail(const char* state) {
      work.accepted.value.valid = false;
      work.phase = Phase::Idle;
      work.remaining = 0;
      work.state = state;
      work.blocked = true;
      work.requestPending = false;
      if (settings.dev_mode) ++work.counters.fallbacks;
    }
    void account(uint32_t start, bool update = false, Phase phase = Phase::Idle) {
      const uint32_t elapsed = uint32_t(micros()) - start;
      work.counters.fixWorkUs += elapsed;
      work.counters.maxFixWorkUs = std::max(work.counters.maxFixWorkUs, work.counters.fixWorkUs);
      if (update)
        work.counters.maxUpdateUs = std::max(work.counters.maxUpdateUs, elapsed);
      else {
        work.counters.maxSliceUs = std::max(work.counters.maxSliceUs, elapsed);
        if (elapsed > task_timer::OPTIMIZER_BUDGET_US) ++work.counters.slicesOverBudget;
        if (phase == Phase::Geometry)
          work.counters.maxGeometryUs = std::max(work.counters.maxGeometryUs, elapsed);
        if (phase == Phase::Publish)
          work.counters.maxPublishPointUs = std::max(work.counters.maxPublishPointUs, elapsed);
      }
    }
    void beginPublish() {
      work.phase = Phase::Publish;
      work.index = 0;
      work.building = {};
      work.building.sourceFixMs = work.lastFix;
      work.previous = work.pilot;
      work.previousGeo = {work.fix.latitude, work.fix.longitude};
      work.buildStartedMs = settings.dev_mode ? millis() : 0;
    }
    void reprice(const Navigator& nav) {
      auto& p = work.accepted;
      if (!p.value.valid) return;
      const Geo pilot{work.fix.latitude, work.fix.longitude};
      const double cylinder =
          entryDistance(pilot, p.aim, geo(nav.activePoint), nav.activeCylinderRadiusM());
      if (!std::isfinite(cylinder)) {
        fail("intercept_unavailable");
        return;
      }
      p.value.cylinderM = cylinder;
      p.value.goalM = work.count == 1 ? cylinder : distance(pilot, p.aim) + p.suffixM;
      p.value.bearingDeg =
          distance(pilot, p.aim) < 0.05 ? work.fix.courseDeg : bearing(pilot, p.aim);
      p.value.followingBearingDeg = p.value.hasFollowing ? bearing(pilot, p.following) : 0;
      if (work.firstLeg) {
        p.value.initialLegM = cylinder;
        work.firstLeg = false;
      }
    }
    void restart(const Navigator& nav, uint32_t hash) {
      work.hash = hash;
      work.route = uint8_t(nav.activeRouteIndex);
      work.active = int16_t(nav.activeRoutePointIndex);
      work.count = nav.routes[nav.activeRouteIndex].totalPoints - work.active + 1;
      work.accepted = {};
      work.initialized = false;
      work.blocked = false;
      work.firstLeg = true;
      work.index = 0;
      work.sincePublish = 0;
      work.remaining = 64;
      work.phase = Phase::Geometry;
      work.projection.reset({work.fix.latitude, work.fix.longitude});
      work.pilot = {};
      work.state = "warming";
      work.restartStartedMs = work.requestStartedMs = settings.dev_mode ? millis() : 0;
      work.requestPending = settings.dev_mode;
      if (settings.dev_mode) ++work.counters.restarts;
    }
    bool supported(const Navigator& nav) {
      const auto& route = nav.routes[nav.activeRouteIndex];
      // Timed starts and goal lines are stored as task metadata but require different
      // scoring geometry. Do not quietly reinterpret them as cylinder-entry tasks.
      if (route.goalType != RouteGoalType::Cylinder || route.startType != RouteStartType::None)
        return false;
      for (int i = int16_t(nav.activeRoutePointIndex); i <= route.totalPoints; ++i)
        if (nav.routePointMeta(nav.activeRouteIndex, RouteIndex(i)).role ==
            RoutePointRole::StartSpeedSection)
          return false;
      return true;
    }
  }  // namespace

  void invalidate() {
    work.phase = Phase::Idle;
    work.accepted.value.valid = false;
    work.initialized = false;
    work.blocked = false;
    work.haveFix = false;
    work.route = 0;
    work.count = 0;
    work.remaining = 0;
    work.requestPending = false;
    work.state = "idle";
  }
  void update(const Navigator& nav) {
    const bool measure = settings.dev_mode;
    const uint32_t start = measure ? micros() : 0;
    if (!eligible(nav)) {
      invalidate();
      if (measure) account(start, true);
      return;
    }
    GPSPositionSnapshot fix;
    if (!gps.lastValidFix(fix)) {
      invalidate();
      if (measure) account(start, true);
      return;
    }
    // GGA and RMC can commit the same position epoch at different receipt times.
    // Use their UTC epoch when available; a second scheduled GPS poll is not
    // a second optimization budget for the same fix.
    const bool hasUtc = gps.time.isValid() && gps.time.age() < 2000;
    const uint32_t utc = hasUtc ? gps.time.value() : 0;
    const bool fresh = !work.haveFix || (hasUtc ? !work.haveUtc || utc != work.lastUtc
                                                : fix.capturedAtMs != work.lastFix);
    work.haveUtc = hasUtc;
    work.lastUtc = utc;
    if (fresh) {
      work.lastFix = fix.capturedAtMs;
      work.haveFix = true;
      if (measure) {
        work.counters.lastFixWorkUs = work.counters.fixWorkUs;
        work.counters.fixWorkUs = 0;
        ++work.counters.fixes;
        // Keep the oldest outstanding request so new fixes cannot conceal starvation.
        if (!work.requestPending && !work.blocked) {
          work.requestStartedMs = millis();
          work.requestPending = true;
        }
      }
    }
    work.fix = fix;
    const uint32_t hash = routeHash(nav);
    const uint8_t active = int16_t(nav.activeRoutePointIndex);
    if (work.route != uint8_t(nav.activeRouteIndex) || hash != work.hash || active < work.active ||
        active > work.active + 1 ||
        (!work.initialized && work.phase == Phase::Idle && !work.blocked)) {
      restart(nav, hash);
      if (!work.count || work.count > maxRoutePointRefs)
        fail("route_capacity");
      else if (!supported(nav))
        fail("unsupported_task");
    } else if (active != work.active) {
      if (work.initialized && work.solver.advance()) {
        work.active = active;
        --work.count;
        work.firstLeg = true;
        work.accepted = {};
        work.restartStartedMs = work.requestStartedMs = measure ? millis() : 0;
        work.requestPending = measure;
        work.remaining = std::max<uint16_t>(work.remaining, 16);
        beginPublish();
      } else
        restart(nav, hash);
    }
    if (!work.blocked) {
      if (!work.projection.forward({fix.latitude, fix.longitude}, work.pilot))
        fail("projection_extent");
      else {
        if (work.initialized) work.solver.updatePosition(work.pilot);
        if (fresh) work.remaining = std::max<uint16_t>(work.remaining, 16);
        reprice(nav);
        if (work.phase == Phase::Idle && work.remaining && work.initialized)
          work.phase = Phase::Iterate;
      }
    }
    if (measure) account(start, true);
  }
  bool hasPendingWork() { return work.phase != Phase::Idle; }

  void service(const Navigator& nav) {
    if (!eligible(nav)) {
      if (work.route) invalidate();
      return;
    }
    if (work.phase == Phase::Idle || work.blocked) return;
    // Geometry changes are processed by Navigator::update before service resumes.
    if (work.route != uint8_t(nav.activeRouteIndex) ||
        work.active != int16_t(nav.activeRoutePointIndex))
      return;
    const bool measure = settings.dev_mode;
    const uint32_t start = measure ? micros() : 0;
    const Phase phase = work.phase;
    if (work.phase == Phase::Geometry) {
      Disk disk;
      if (!work.projection.forward(geo(point(nav, work.index)), disk.center))
        fail("projection_extent");
      else {
        disk.radius = meta(nav, work.index).radiusM;
        if (!work.projection.supports(disk))
          fail("projection_distortion");
        else {
          work.solver.disks[work.index] = disk;
          if (++work.index == work.count) {
            work.initialized = work.solver.initialize(work.count, work.pilot);
            if (work.initialized)
              beginPublish();
            else
              fail("invalid_geometry");
          }
        }
      }
    } else if (work.phase == Phase::Iterate) {
      const bool ok = work.solver.iterate();
      if (measure) {
        const uint32_t elapsed = uint32_t(micros()) - start;
        work.counters.maxIterationUs = std::max(work.counters.maxIterationUs, elapsed);
        ++work.counters.iterations;
      }
      if (!ok)
        fail("solver_failure");
      else {
        if (work.remaining) --work.remaining;
        // Publish regularly during cold convergence. No iteration runs inside GPS parsing.
        if (++work.sincePublish >= 16 || !work.remaining) {
          work.sincePublish = 0;
          beginPublish();
        }
      }
    } else if (work.phase == Phase::Publish) {
      const auto& disk = work.solver.disks[work.index];
      float margin = 0;
      Vec adjusted =
          conservativePoint(work.solver.intercept(work.index), work.previous, disk, &margin);
      Geo target = work.projection.inverse(adjusted);
      const Geo center = geo(point(nav, work.index));
      const double radius = meta(nav, work.index).radiusM;
      // Correct the <=0.5 m projection discrepancy on the real spherical cylinder.
      // A tiny numerical inset also makes an unbuffered boundary aim reachable
      // under the sequencer's strict '< radius' comparison.
      const double safeRadius = std::max(0.0, radius - margin - 0.05);
      if (distance(center, target) > safeRadius) {
        target = destination(center, bearing(center, target), safeRadius);
        if (!work.projection.forward(target, adjusted)) {
          fail("projection_extent");
          if (measure) account(start, false, phase);
          return;
        }
      }
      const double leg = distance(work.previousGeo, target);
      if (work.index == 0) {
        work.building.aim = target;
        work.building.value.marginM = margin;
        work.building.value.targetLatitude = target.lat;
        work.building.value.targetLongitude = target.lon;
      }
      if (work.index == 1) work.building.following = target;
      const bool last = work.index + 1 == work.count;
      const double segment = last ? entryDistance(work.previousGeo, target, center, radius) : leg;
      if (!std::isfinite(segment))
        fail("intercept_unavailable");
      else {
        if (work.index > 0) work.building.suffixM += segment;
        work.previous = adjusted;
        work.previousGeo = target;
        if (++work.index == work.count) {
          const bool firstPlan = !work.accepted.value.valid;
          const double initial = work.accepted.value.initialLegM;
          work.accepted = work.building;
          work.accepted.value.valid = true;
          work.accepted.value.hasFollowing = work.count > 1;
          work.accepted.value.initialLegM = initial;
          reprice(nav);
          if (!work.blocked) {
            if (measure) {
              const uint32_t now = millis();
              if (work.requestPending) {
                work.counters.lastRequestLatencyMs = now - work.requestStartedMs;
                work.counters.maxRequestLatencyMs =
                    std::max(work.counters.maxRequestLatencyMs, work.counters.lastRequestLatencyMs);
              }
              if (firstPlan && work.restartStartedMs)
                work.counters.maxFirstPlanLatencyMs =
                    std::max(work.counters.maxFirstPlanLatencyMs, now - work.restartStartedMs);
              ++work.counters.publications;
              if (work.buildStartedMs)
                work.counters.maxPublicationLatencyMs =
                    std::max(work.counters.maxPublicationLatencyMs, now - work.buildStartedMs);
            }
            work.requestPending = false;
            work.state = "tracking";
            work.phase = work.remaining ? Phase::Iterate : Phase::Idle;
            // Publish bearings and dependent glide together; this refresh does not
            // enqueue additional work for an already processed position fix.
            const uint32_t refreshStart = measure ? micros() : 0;
            navigator.refreshRouteGuidance();
            if (measure)
              work.counters.maxRefreshUs =
                  std::max(work.counters.maxRefreshUs, uint32_t(micros()) - refreshStart);
          }
        }
      }
    }
    if (measure) account(start, false, phase);
  }
  void skippedSlice() {
    if (settings.dev_mode && hasPendingWork()) ++work.counters.skippedSlices;
  }
  const Solution* solution(const Navigator& nav) {
    if (!eligible(nav) || !work.accepted.value.valid ||
        work.route != uint8_t(nav.activeRouteIndex) ||
        work.active != int16_t(nav.activeRoutePointIndex))
      return nullptr;
    return &work.accepted.value;
  }
  const Diagnostics& diagnostics() {
    work.counters.workspaceBytes = sizeof(work);
    work.counters.pendingIterations = work.remaining;
    work.counters.planSourceAgeMs =
        work.accepted.value.valid ? uint32_t(millis() - work.accepted.sourceFixMs) : 0;
    return work.counters;
  }
  const char* status() { return work.state; }
  void writeJson(Print& out) {
    const auto& d = diagnostics();
    const auto* s = solution(navigator);
    out.printf(
        "{\"enabled\":true,\"status\":\"%s\",\"valid\":%s,\"workspace_bytes\":%lu,\"fixes\":%lu,"
        "\"iterations\":%lu,\"publications\":%lu,\"restarts\":%lu,\"fallbacks\":%lu,\"pending_"
        "iterations\":%u,\"max_iteration_us\":%lu,\"max_slice_us\":%lu,\"max_update_us\":%lu,\"max_"
        "fix_work_us\":%lu,\"last_fix_work_us\":%lu,\"max_publication_latency_ms\":%lu,\"skipped_"
        "slices\":%lu",
        status(), s ? "true" : "false", (unsigned long)d.workspaceBytes, (unsigned long)d.fixes,
        (unsigned long)d.iterations, (unsigned long)d.publications, (unsigned long)d.restarts,
        (unsigned long)d.fallbacks, d.pendingIterations, (unsigned long)d.maxIterationUs,
        (unsigned long)d.maxSliceUs, (unsigned long)d.maxUpdateUs, (unsigned long)d.maxFixWorkUs,
        (unsigned long)d.lastFixWorkUs, (unsigned long)d.maxPublicationLatencyMs,
        (unsigned long)d.skippedSlices);
    const char* phase = work.phase == Phase::Geometry  ? "geometry"
                        : work.phase == Phase::Iterate ? "iterate"
                        : work.phase == Phase::Publish ? "publish"
                                                       : "idle";
    out.printf(
        ",\"route_capacity\":%u,\"remaining_points\":%u,\"phase\":\"%s\","
        "\"max_geometry_us\":%lu,\"max_publish_point_us\":%lu,\"max_refresh_us\":%lu,"
        "\"slices_over_budget\":%lu,\"max_request_latency_ms\":%lu,"
        "\"last_request_latency_ms\":%lu,\"max_first_plan_latency_ms\":%lu,"
        "\"plan_source_age_ms\":%lu,\"pending_request_age_ms\":%lu",
        unsigned(maxRoutePointRefs), unsigned(work.count), phase, (unsigned long)d.maxGeometryUs,
        (unsigned long)d.maxPublishPointUs, (unsigned long)d.maxRefreshUs,
        (unsigned long)d.slicesOverBudget, (unsigned long)d.maxRequestLatencyMs,
        (unsigned long)d.lastRequestLatencyMs, (unsigned long)d.maxFirstPlanLatencyMs,
        (unsigned long)d.planSourceAgeMs,
        (unsigned long)(work.requestPending ? uint32_t(millis() - work.requestStartedMs) : 0));
    if (s)
      out.printf(
          ",\"goal_m\":%.3f,\"cylinder_m\":%.3f,\"margin_m\":%.3f,\"target_latitude\":%.8f,"
          "\"target_longitude\":%.8f,\"has_following\":%s",
          s->goalM, s->cylinderM, s->marginM, s->targetLatitude, s->targetLongitude,
          s->hasFollowing ? "true" : "false");
    heap_monitor::Snapshot memory{};
    heap_monitor::snapshot(memory);
    out.printf(
        ",\"internal_free_bytes\":%lu,\"internal_min_bytes\":%lu,\"internal_largest_bytes\":%lu,"
        "\"loop_stack_free_bytes\":%lu",
        (unsigned long)memory.internalFree, (unsigned long)memory.internalMin,
        (unsigned long)memory.internalLargest, (unsigned long)memory.loopStack);
    out.print('}');
  }
}  // namespace route_guidance
