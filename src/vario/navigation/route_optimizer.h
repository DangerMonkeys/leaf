#pragma once
// Fixed-storage geometry and primal-dual optimizer shared by simulator and device.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace route_optimizer {
  constexpr double pi = 3.14159265358979323846;
  // Match TinyGPSPlus::distanceBetween and the existing waypoint sequencer.
  constexpr double earthRadiusM = 6372795.0;
  struct Vec {
    float x = 0, y = 0;
    Vec operator+(Vec b) const { return {x + b.x, y + b.y}; }
    Vec operator-(Vec b) const { return {x - b.x, y - b.y}; }
    Vec operator*(float s) const { return {x * s, y * s}; }
  };
  inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
  inline float length(Vec a) { return std::sqrt(dot(a, a)); }
  struct Disk {
    Vec center;
    float radius = 0;
  };
  inline Vec project(Vec p, Disk d) {
    const Vec v = p - d.center;
    const float v2 = dot(v, v);
    return v2 <= d.radius * d.radius || v2 == 0 ? p : d.center + v * (d.radius / std::sqrt(v2));
  }
  inline Vec conservativePoint(Vec raw, Vec previous, Disk disk, float* margin = nullptr) {
    if (disk.radius < 20) {
      if (margin) *margin = disk.radius;
      return disk.center;
    }
    const Vec incoming = raw - previous, inward = disk.center - raw;
    const float denominator = dot(incoming, incoming) * dot(inward, inward);
    const float radialCos2 =
        denominator > 1e-12f ? dot(incoming, inward) * dot(incoming, inward) / denominator : 1;
    // Zero at >=45 degrees away from tangent; 20 m at tangent. No inverse trig.
    const float m = 20 * std::clamp(1 - 2 * radialCos2, 0.0f, 1.0f);
    if (margin) *margin = m;
    return project(raw, {disk.center, std::max(0.0f, disk.radius - m)});
  }

  // Chambolle-Pock, tau*sigma=0.24 < 1/||K||^2. No allocation, recursion, or exceptions.
  template <size_t Capacity>
  class Solver {
   public:
    std::array<Disk, Capacity> disks{};
    bool initialize(size_t n, Vec start) {
      if (!n || n > Capacity || !std::isfinite(start.x) || !std::isfinite(start.y)) return false;
      n_ = n;
      start_ = start;
      float radii = 0;
      for (size_t i = 0; i < n_; ++i) {
        const auto& d = disks[i];
        if (!std::isfinite(d.center.x) || !std::isfinite(d.center.y) || !std::isfinite(d.radius) ||
            d.radius < 0)
          return false;
        points_[i] = best_[i] = extra_[i] = d.center;
        dual_[i] = {};
        radii += d.radius;
      }
      tau_ = std::max(100.0f, radii / n_);
      sigma_ = 0.24f / tau_;
      bestDistance_ = distance(best_);
      return true;
    }
    size_t size() const { return n_; }
    Vec intercept(size_t i) const { return best_[i]; }
    float bestDistance() const { return bestDistance_; }
    void updatePosition(Vec start) {
      start_ = start;
      // Keep the chosen point array frozen while a guidance snapshot is assembled.
      bestDistance_ = distance(best_);
    }
    bool advance() {
      if (n_ <= 1) return false;
      for (size_t i = 1; i < n_; ++i) {
        disks[i - 1] = disks[i];
        points_[i - 1] = points_[i];
        best_[i - 1] = best_[i];
        dual_[i - 1] = dual_[i];
      }
      --n_;
      extra_ = points_;
      bestDistance_ = distance(best_);
      return true;
    }
    bool iterate() {
      if (!n_) return false;
      if (n_ == 1) {
        points_[0] = project(start_, disks[0]);
        extra_[0] = points_[0];
        dual_[0] = {};
      } else {
        for (size_t i = 0; i < n_; ++i) {
          const Vec prev = i ? extra_[i - 1] : start_;
          dual_[i] = project(dual_[i] + (extra_[i] - prev) * sigma_, {{0, 0}, 1});
        }
        for (size_t i = 0; i < n_; ++i) {
          const Vec old = points_[i], next = i + 1 < n_ ? dual_[i + 1] : Vec{};
          points_[i] = project(old - (dual_[i] - next) * tau_, disks[i]);
          extra_[i] = points_[i] * 2 - old;
        }
      }
      const float candidate = distance(points_);
      if (!std::isfinite(candidate)) return false;
      if (candidate <= bestDistance_) {
        bestDistance_ = candidate;
        best_ = points_;
      }
      return true;
    }

   private:
    using Points = std::array<Vec, Capacity>;
    float distance(const Points& p) const {
      float sum = 0;
      Vec prev = start_;
      for (size_t i = 0; i < n_; ++i) {
        sum += length(p[i] - prev);
        prev = p[i];
      }
      return sum;
    }
    Points points_{}, best_{}, extra_{}, dual_{};
    Vec start_{};
    size_t n_ = 0;
    float bestDistance_ = 0, tau_ = 100, sigma_ = 0.0024f;
  };

  struct Geo {
    double lat = 0, lon = 0;
  };
  inline double toRadians(double degrees) { return degrees * pi / 180; }
  inline double toDegrees(double radians) { return radians * 180 / pi; }
  inline double angularDistance(Geo a, Geo b) {
    const double x = std::sin(toRadians(b.lat - a.lat) / 2),
                 y = std::sin(toRadians(b.lon - a.lon) / 2);
    const double h = std::clamp(
        x * x + std::cos(toRadians(a.lat)) * std::cos(toRadians(b.lat)) * y * y, 0.0, 1.0);
    return 2 * std::atan2(std::sqrt(h), std::sqrt(1 - h));
  }
  inline double distance(Geo a, Geo b) { return earthRadiusM * angularDistance(a, b); }
  inline double bearing(Geo a, Geo b) {
    const double d = toRadians(b.lon - a.lon), x = std::sin(d) * std::cos(toRadians(b.lat));
    const double y = std::cos(toRadians(a.lat)) * std::sin(toRadians(b.lat)) -
                     std::sin(toRadians(a.lat)) * std::cos(toRadians(b.lat)) * std::cos(d);
    return toDegrees(std::atan2(x, y));
  }
  inline Geo destination(Geo from, double bearingDeg, double distanceM) {
    const double lat = toRadians(from.lat), angle = distanceM / earthRadiusM,
                 course = toRadians(bearingDeg);
    const double targetLat = std::asin(std::clamp(
        std::sin(lat) * std::cos(angle) + std::cos(lat) * std::sin(angle) * std::cos(course), -1.0,
        1.0));
    const double lon =
        toRadians(from.lon) + std::atan2(std::sin(course) * std::sin(angle) * std::cos(lat),
                                         std::cos(angle) - std::sin(lat) * std::sin(targetLat));
    return {toDegrees(targetLat), std::remainder(toDegrees(lon), 360.0)};
  }
  // First intersection of the great-circle segment toward the interior aim point
  // with the actual spherical waypoint cylinder. Stable even for small radii.
  inline double entryDistance(Geo from, Geo aim, Geo center, double radiusM) {
    const double dc = angularDistance(from, center), radius = radiusM / earthRadiusM;
    if (dc <= radius) return 0;
    const double direction = toRadians(bearing(from, center) - bearing(from, aim));
    const double cross = std::sin(dc) * std::sin(direction);
    const double along = std::atan2(std::sin(dc) * std::cos(direction), std::cos(dc));
    const double disc = std::sin(radius) * std::sin(radius) - cross * cross;
    if (disc < -1e-15) return NAN;
    const double half = std::atan2(std::sqrt(std::max(0.0, disc)), std::cos(radius));
    const double result = (along - half) * earthRadiusM, leg = distance(from, aim);
    return result >= -0.05 && result <= leg + 0.05 ? std::clamp(result, 0.0, leg) : NAN;
  }
  // Spherical azimuthal equidistant projection. Unlike the desktop lab's flat
  // latitude scale this handles high latitudes and longitude wrapping.
  class Projection {
   public:
    void reset(Geo origin) { origin_ = origin; }
    bool forward(Geo geo, Vec& result) const {
      const double d = distance(origin_, geo), a = toRadians(bearing(origin_, geo));
      if (!std::isfinite(d) || d > 200000) return false;
      result = {float(d * std::sin(a)), float(d * std::cos(a))};
      return true;
    }
    Geo inverse(Vec p) const {
      return destination(origin_, toDegrees(std::atan2(p.x, p.y)), length(p));
    }
    // AEQD stretches distances by at most c/sin(c) within this disk. Keep
    // cylinder-shape error below 0.5 m; unsupported geometry uses native nav.
    bool supports(Disk disk) const {
      const double reach = length(disk.center) + disk.radius, c = reach / earthRadiusM;
      const double stretch = c > 1e-8 ? c / std::sin(c) - 1 : 0;
      return reach <= 200000 && stretch * disk.radius <= 0.5;
    }

   private:
    Geo origin_{};
  };
  static_assert(sizeof(Solver<20>) < 1024, "route solver storage budget");
}  // namespace route_optimizer
