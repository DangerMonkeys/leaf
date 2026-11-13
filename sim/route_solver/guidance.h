// Shared desktop experiment policy. No hardware build includes this header.
#pragma once
#include <cstring>
#include "solver.h"

namespace route_experiment {
  enum class MarginPolicy {
    None,
    Fixed25,
    Fixed50,
    Angle0,
    Angle5,
    Grazing20,
    Grazing30,
    Grazing40,
    Centers
  };
  inline const char* policyName(MarginPolicy p) {
    switch (p) {
      case MarginPolicy::None:
        return "none";
      case MarginPolicy::Fixed25:
        return "fixed25";
      case MarginPolicy::Fixed50:
        return "fixed50";
      case MarginPolicy::Angle0:
        return "angle0_50";
      case MarginPolicy::Angle5:
        return "angle5_50";
      case MarginPolicy::Grazing20:
        return "grazing20";
      case MarginPolicy::Grazing30:
        return "grazing30";
      case MarginPolicy::Grazing40:
        return "grazing40";
      case MarginPolicy::Centers:
        return "centers";
    }
    return "none";
  }
  inline bool parsePolicy(const char* name, MarginPolicy& p) {
    for (auto option : {MarginPolicy::None, MarginPolicy::Fixed25, MarginPolicy::Fixed50,
                        MarginPolicy::Angle0, MarginPolicy::Angle5, MarginPolicy::Grazing20,
                        MarginPolicy::Grazing30, MarginPolicy::Grazing40, MarginPolicy::Centers})
      if (std::strcmp(name, policyName(option)) == 0) {
        p = option;
        return true;
      }
    return false;
  }
  struct AdjustedPoint {
    Vec point;
    float margin = 0, grazing = 0;
  };
  inline AdjustedPoint conservativePoint(Vec raw, Vec previous, Disk disk, MarginPolicy policy) {
    AdjustedPoint out{raw, 0, 0};
    if (policy == MarginPolicy::None) return out;
    if (policy == MarginPolicy::Centers) return {disk.center, disk.radius, 0};
    const bool grazingOnly = policy == MarginPolicy::Grazing20 ||
                             policy == MarginPolicy::Grazing30 || policy == MarginPolicy::Grazing40;
    const float maximum = policy == MarginPolicy::Fixed25     ? 25
                          : policy == MarginPolicy::Grazing20 ? 20
                          : policy == MarginPolicy::Grazing30 ? 30
                          : policy == MarginPolicy::Grazing40 ? 40
                                                              : 50;
    if (disk.radius <= maximum) return {disk.center, disk.radius, 0};
    const Vec incoming = raw - previous, inward = disk.center - raw;
    const float denominator = dot(incoming, incoming) * dot(inward, inward);
    out.grazing = denominator > 1e-12f
                      ? std::clamp(1 - dot(incoming, inward) * dot(incoming, inward) / denominator,
                                   0.0f, 1.0f)
                      : 0;
    const float minimum = policy == MarginPolicy::Angle5 ? 5 : 0;
    out.margin = (policy == MarginPolicy::Fixed25 || policy == MarginPolicy::Fixed50)
                     ? maximum
                     : minimum + (maximum - minimum) * out.grazing;
    // Angle from the inward radial direction: <=45 degrees needs no margin;
    // ramp in sin-squared space to the maximum at an exactly tangent approach.
    if (grazingOnly) out.margin = maximum * std::clamp(2 * out.grazing - 1, 0.0f, 1.0f);
    Work work;
    out.point = project(raw, {disk.center, disk.radius - out.margin}, work);
    return out;
  }
  // First contact on a segment to an interior aim point. Double intermediates
  // avoid cancellation on nearly tangent legs; storage remains float pairs.
  inline Vec cylinderEntry(Vec from, Vec target, Disk disk) {
    const Vec d = target - from, offset = from - disk.center;
    const double a = double(d.x) * d.x + double(d.y) * d.y;
    const double c = double(offset.x) * offset.x + double(offset.y) * offset.y -
                     double(disk.radius) * disk.radius;
    if (c <= 0) return from;
    if (a < 1e-12) return target;
    const double b = double(offset.x) * d.x + double(offset.y) * d.y;
    const double discriminant = b * b - a * c;
    if (discriminant < 0) return target;
    const double t = std::clamp((-b - std::sqrt(discriminant)) / a, 0.0, 1.0);
    return from + d * float(t);
  }
  template <size_t Capacity>
  struct GuidancePlan {
    std::array<Vec, Capacity> points{};
    std::array<float, Capacity> margins{};
    size_t count = 0;
    float distance = 0;
  };
  template <size_t Capacity, typename Solver>
  GuidancePlan<Capacity> guidancePlan(const Solver& solver, const Disk* disks, Vec pilot,
                                      MarginPolicy policy) {
    GuidancePlan<Capacity> plan;
    plan.count = solver.size();
    Vec previous = pilot;
    for (size_t i = 0; i < plan.count; ++i) {
      // Use the already adjusted incoming leg, not an unrelated nominal predecessor.
      const auto adjusted = conservativePoint(solver.intercept(i), previous, disks[i], policy);
      plan.points[i] = adjusted.point;
      plan.margins[i] = adjusted.margin;
      // Flight ends on entry into the actual goal cylinder, before the buffered aim point.
      const Vec endpoint =
          i + 1 == plan.count ? cylinderEntry(previous, adjusted.point, disks[i]) : adjusted.point;
      plan.distance += std::hypot(endpoint.x - previous.x, endpoint.y - previous.y);
      previous = adjusted.point;
    }
    return plan;
  }
  inline double bearing(Vec from, Vec to) {
    return std::atan2(double(to.x) - from.x, double(to.y) - from.y) * 180 / 3.14159265358979323846;
  }
  inline double wrapDegrees(double value) { return std::remainder(value, 360.0); }

  // Shared small routes for repeatable desktop and interactive free-flight review.
  inline size_t reviewRoute(const char* name, Disk* disks) {
    if (std::strcmp(name, "tangent6") == 0) {
      for (size_t i = 0; i < 5; ++i) disks[i] = {{float(i * 800), float(i % 2 ? -400 : 400)}, 400};
      disks[5] = {{4200, 0}, 120};
      return 6;
    }
    if (std::strcmp(name, "mixed7") == 0) {
      // From (-700,0), the ideal unbuffered contact angles are approximately
      // 90, 90, 0 (out-and-back reversal), 90, 45, 60, 0 degrees.
      // Contacts: (0,0), (700,0), (1450,0), (700,0), (-800,0),
      // (-800,-900), (-20.577,-1350). The return leg deliberately retraces east/west.
      const Disk points[] = {{{0, 300}, 300},
                             {{700, -300}, 300},
                             {{1700, 0}, 250},
                             {{700, 300}, 300},
                             {{-1012.132f, 212.132f}, 300},
                             {{-1059.808f, -1050}, 300},
                             {{83.346f, -1410}, 120}};
      std::copy(points, points + 7, disks);
      return 7;
    }
    if (std::strcmp(name, "small6") == 0) {
      for (size_t i = 0; i < 5; ++i) disks[i] = {{float(i * 500), float(i % 2 ? -35 : 35)}, 35};
      disks[5] = {{2600, 0}, 35};
      return 6;
    }
    if (std::strcmp(name, "corners6") == 0) {
      const Disk points[] = {{{400, 400}, 180},  {{400, 1200}, 180}, {{1400, 1200}, 180},
                             {{1400, 200}, 180}, {{2400, 200}, 180}, {{2400, 1200}, 180}};
      std::copy(points, points + 6, disks);
      return 6;
    }
    if (std::strcmp(name, "overlap6") == 0) {
      for (size_t i = 0; i < 6; ++i) disks[i] = {{float(i * 500), float(i % 2 ? 250 : 0)}, 650};
      return 6;
    }
    if (std::strcmp(name, "straight6") == 0) {
      for (size_t i = 0; i < 6; ++i) disks[i] = {{float(400 + i * 650), 0}, 200};
      return 6;
    }
    return 0;
  }
}  // namespace route_experiment
