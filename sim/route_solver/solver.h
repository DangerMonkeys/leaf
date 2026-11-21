// Desktop experiment only. No firmware or Navigator dependencies.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace route_experiment {
  struct Vec {
    float x = 0, y = 0;
    Vec operator+(Vec b) const { return {x + b.x, y + b.y}; }
    Vec operator-(Vec b) const { return {x - b.x, y - b.y}; }
    Vec operator*(float s) const { return {x * s, y * s}; }
  };
  inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
  struct Work {
    uint64_t roots = 0, local_evaluations = 0, point_updates = 0;
  };
  inline float length(Vec a, Work& w) {
    ++w.roots;
    return std::sqrt(dot(a, a));
  }
  struct Disk {
    Vec center;
    float radius = 0;
  };
  inline Vec project(Vec p, Disk d, Work& w) {
    const Vec delta = p - d.center;
    if (dot(delta, delta) <= d.radius * d.radius) return p;
    return d.center + delta * (d.radius / length(delta, w));
  }

  template <size_t Capacity>
  class State {
   public:
    void initialize(const Disk* disks, size_t n, Vec start) {
      if (n == 0 || n > Capacity) throw std::invalid_argument("invalid cylinder count");
      n_ = n;
      start_ = start;
      work = {};
      initialized_ = false;
      for (size_t i = 0; i < n_; ++i) {
        if (!std::isfinite(disks[i].center.x) || !std::isfinite(disks[i].center.y) ||
            !std::isfinite(disks[i].radius) || disks[i].radius < 0)
          throw std::invalid_argument("invalid disk");
        disks_[i] = disks[i];
        points_[i] = best_[i] = disks[i].center;
      }
      consider();
    }
    void update_position(Vec start) {
      start_ = start;
      // Reprice both paths; an objective from an old GPS fix is invalid.
      best_distance_ = distance(best_);
      consider();
    }
    void advance_waypoint() {
      if (n_ <= 1) throw std::invalid_argument("cannot remove final cylinder");
      for (size_t i = 1; i < n_; ++i) {
        disks_[i - 1] = disks_[i];
        points_[i - 1] = points_[i];
        best_[i - 1] = best_[i];
      }
      --n_;
      best_distance_ = distance(best_);
      consider();
    }
    float best_distance() const { return best_distance_; }
    size_t size() const { return n_; }
    Vec intercept(size_t i) const { return best_[i]; }
    Work work;

   protected:
    using Points = std::array<Vec, Capacity>;
    bool solve_single(unsigned iterations) {
      if (n_ != 1 || iterations == 0) return false;
      ++work.point_updates;
      points_[0] = project(start_, disks_[0], work);
      consider();
      return true;
    }
    float distance(const Points& points) {
      float sum = 0;
      Vec prev = start_;
      for (size_t i = 0; i < n_; ++i) {
        sum += length(points[i] - prev, work);
        prev = points[i];
      }
      return sum;
    }
    void consider() {
      const float candidate = distance(points_);
      if (!initialized_ || candidate <= best_distance_) {
        best_distance_ = candidate;
        best_ = points_;
        initialized_ = true;
      }
    }
    std::array<Disk, Capacity> disks_{};
    Points points_{}, best_{};
    Vec start_{};
    size_t n_ = 0;
    float best_distance_ = 0;
    bool initialized_ = false;
  };

  // Block-coordinate descent: segment/disk shortcut, otherwise bounded
  // golden-section minimization of the visible boundary arc.
  template <size_t Capacity>
  class Relaxation : public State<Capacity> {
    using Base = State<Capacity>;
    using Base::disks_;
    using Base::n_;
    using Base::points_;
    using Base::start_;
    using Base::work;

   public:
    void improve(unsigned iterations, size_t prefix = Capacity) {
      if (this->solve_single(iterations)) return;
      const size_t count = std::min(prefix, n_);
      for (unsigned pass = 0; pass < iterations; ++pass) {
        for (size_t i = 0; i < count; ++i) adjust(i);
        for (size_t i = count; i-- > 0;) adjust(i);
        this->consider();
      }
    }

   private:
    float cost(Vec p, Vec a, Vec b) {
      ++work.local_evaluations;
      return length(p - a, work) + length(p - b, work);
    }
    void adjust(size_t i) {
      ++work.point_updates;
      const Disk d = disks_[i];
      const Vec a = i ? points_[i - 1] : start_;
      if (i + 1 == n_) {
        points_[i] = project(a, d, work);
        return;
      }
      const Vec b = points_[i + 1], v = b - a;
      const float vv = dot(v, v);
      const float t = vv > 0 ? std::clamp(dot(d.center - a, v) / vv, 0.0f, 1.0f) : 0;
      const Vec closest = a + v * t;
      if (dot(closest - d.center, closest - d.center) <= d.radius * d.radius) {
        // Prefer the old intercept's projection in a flat objective.
        const float middle = vv > 0 ? dot(d.center - a, v) / vv : 0;
        const Vec foot = a + v * middle;
        const float half =
            vv > 0 ? std::sqrt(std::max(
                         0.0f, (d.radius * d.radius - dot(foot - d.center, foot - d.center)) / vv))
                   : 0;
        if (vv > 0) ++work.roots;
        const float old = vv > 0 ? dot(points_[i] - a, v) / vv : 0;
        points_[i] =
            a + v * std::clamp(old, std::max(0.0f, middle - half), std::min(1.0f, middle + half));
        return;
      }
      constexpr float pi = 3.14159265358979323846f, ratio = 0.61803398875f;
      const float angle = std::atan2(a.y - d.center.y, a.x - d.center.x);
      const float delta =
          std::remainder(std::atan2(b.y - d.center.y, b.x - d.center.x) - angle, 2 * pi);
      float lo = std::min(angle, angle + delta), hi = std::max(angle, angle + delta);
      auto point = [&](float theta) {
        return d.center + Vec{std::cos(theta), std::sin(theta)} * d.radius;
      };
      float x = hi - ratio * (hi - lo), y = lo + ratio * (hi - lo);
      float fx = cost(point(x), a, b), fy = cost(point(y), a, b);
      for (unsigned k = 0; k < 18; ++k) {
        if (fx < fy) {
          hi = y;
          y = x;
          fy = fx;
          x = hi - ratio * (hi - lo);
          fx = cost(point(x), a, b);
        } else {
          lo = x;
          x = y;
          fx = fy;
          y = lo + ratio * (hi - lo);
          fy = cost(point(y), a, b);
        }
      }
      const Vec candidate = project(point(fx < fy ? x : y), d, work);
      if (cost(candidate, a, b) <= cost(points_[i], a, b)) points_[i] = candidate;
    }
  };

  // Chambolle-Pock for sum_i |q_i-q_(i-1)| + indicator_disks(q).
  // K is the first-difference operator, ||K|| <= 2; tau*sigma = 0.24 < 1/4.
  template <size_t Capacity>
  class PrimalDual : public State<Capacity> {
    using Base = State<Capacity>;
    using Base::disks_;
    using Base::n_;
    using Base::points_;
    using Base::start_;
    using Base::work;

   public:
    void initialize(const Disk* d, size_t n, Vec p) {
      Base::initialize(d, n, p);
      extrapolated_ = points_;
      dual_ = {};
      float mean_radius = 0;
      for (size_t i = 0; i < n_; ++i) mean_radius += d[i].radius;
      tau_ = std::max(100.0f, mean_radius / n_);
      sigma_ = 0.24f / tau_;
    }
    void advance_waypoint() {
      Base::advance_waypoint();
      for (size_t i = 0; i < n_; ++i) dual_[i] = dual_[i + 1];
      extrapolated_ = points_;
    }
    void improve(unsigned iterations, size_t prefix = Capacity) {
      if (this->solve_single(iterations)) {
        extrapolated_[0] = points_[0];
        dual_[0] = {};
        return;
      }
      const size_t count = std::min(prefix, n_);
      // A frozen suffix must have no extrapolation left from an earlier full pass.
      for (size_t i = count; i < n_; ++i) extrapolated_[i] = points_[i];
      for (unsigned pass = 0; pass < iterations; ++pass) {
        for (size_t i = 0; i < n_; ++i) {
          const Vec prev = i ? extrapolated_[i - 1] : start_;
          dual_[i] = project(dual_[i] + (extrapolated_[i] - prev) * sigma_, {{0, 0}, 1}, work);
        }
        for (size_t i = 0; i < count; ++i) {
          ++work.point_updates;
          const Vec old = points_[i];
          const Vec next = i + 1 < n_ ? dual_[i + 1] : Vec{};
          points_[i] = project(old - (dual_[i] - next) * tau_, disks_[i], work);
          extrapolated_[i] = points_[i] * 2 - old;
        }
        this->consider();
      }
    }
    // Analytic dual objective. Float projections introduce rounding; the desktop
    // assessment explicitly checks this bound against an independent reference.
    double lower_bound() {
      double bound = -double(dual_[0].x) * start_.x - double(dual_[0].y) * start_.y;
      for (size_t i = 0; i < n_; ++i) {
        const Vec a = dual_[i] - (i + 1 < n_ ? dual_[i + 1] : Vec{});
        bound += double(a.x) * disks_[i].center.x + double(a.y) * disks_[i].center.y -
                 double(disks_[i].radius) * length(a, work);
      }
      return std::max(0.0, bound);
    }

   private:
    typename Base::Points extrapolated_{}, dual_{};
    float tau_ = 100, sigma_ = 0.0024f;
  };
  static_assert(sizeof(Relaxation<10>) < 1024, "typical relaxation memory budget");
  static_assert(sizeof(PrimalDual<10>) < 1024, "typical primal-dual memory budget");
  static_assert(sizeof(PrimalDual<40>) < 2048, "maximum persistent memory budget");
}  // namespace route_experiment
