#include <cassert>
#include <iostream>
#include "guidance.h"
using namespace route_experiment;
int main() {
  const Disk disk{{0, 400}, 400};
  auto tangent = conservativePoint({0, 0}, {-500, 0}, disk, MarginPolicy::Angle0);
  assert(std::abs(tangent.margin - 50) < .001 && std::abs(tangent.point.y - 50) < .001);
  auto radial = conservativePoint({0, 0}, {0, -500}, disk, MarginPolicy::Angle0);
  assert(radial.margin == 0 && radial.point.y == 0);
  for (float angle : {0.0f, 30.0f, 45.0f, 60.0f, 90.0f}) {
    const float a = angle * 3.14159265358979323846f / 180;
    const Vec previous{-500 * std::sin(a), -500 * std::cos(a)};
    const auto reduced = conservativePoint({0, 0}, previous, disk, MarginPolicy::Grazing40);
    const float expected = angle <= 45 ? 0 : angle == 60 ? 20 : 40;
    assert(std::abs(reduced.margin - expected) < .001);
  }
  for (auto choice : {MarginPolicy::Grazing20, MarginPolicy::Grazing30, MarginPolicy::Grazing40}) {
    MarginPolicy parsed;
    assert(parsePolicy(policyName(choice), parsed) && choice == parsed);
  }
  auto floor = conservativePoint({0, 0}, {0, -500}, disk, MarginPolicy::Angle5);
  assert(floor.margin == 5 && floor.point.y == 5);
  auto center = conservativePoint({0, 0}, {-500, 0}, {{0, 30}, 30}, MarginPolicy::Angle0);
  assert(center.point.y == 30 && center.margin == 30);
  auto interior = conservativePoint({0, 300}, {-500, 0}, disk, MarginPolicy::Fixed50);
  assert(interior.point.y == 300);
  auto zero = conservativePoint({0, 400}, {0, 400}, disk, MarginPolicy::Angle5);
  assert(std::isfinite(zero.point.y));
  Disk route[40];
  size_t n = reviewRoute("tangent6", route);
  PrimalDual<40> solver;
  Vec pilot{-700, 0};
  solver.initialize(route, n, pilot);
  solver.improve(128);
  auto plan = guidancePlan<40>(solver, route, pilot, MarginPolicy::Angle5);
  double sum = 0;
  Vec previous = pilot;
  for (size_t i = 0; i < n; ++i) {
    assert(std::hypot(plan.points[i].x - route[i].center.x, plan.points[i].y - route[i].center.y) <=
           route[i].radius - plan.margins[i] + .01);
    const auto end =
        i + 1 == n ? cylinderEntry(previous, plan.points[i], route[i]) : plan.points[i];
    sum += std::hypot(end.x - previous.x, end.y - previous.y);
    previous = plan.points[i];
  }
  assert(std::abs(sum - plan.distance) < .01);
  const auto headOnEntry = cylinderEntry({-500, 0}, {-50, 0}, {{0, 0}, 100});
  assert(std::abs(headOnEntry.x + 100) < .001 && headOnEntry.y == 0);
  const auto obliqueEntry = cylinderEntry({-500, 80}, {0, 80}, {{0, 0}, 100});
  assert(std::abs(obliqueEntry.x + 60) < .001);
  assert(cylinderEntry({0, 0}, {50, 0}, {{0, 0}, 100}).x == 0);
  assert(std::abs(bearing({0, 0}, {1, 0}) - 90) < .001);
  assert(std::abs(wrapDegrees(359 - 1) + 2) < .001);
  // Mixed preset must exercise genuinely different approach geometries after
  // optimization, rather than just looking different when connecting centers.
  n = reviewRoute("mixed7", route);
  assert(n == 7);
  solver.initialize(route, n, pilot);
  solver.improve(4096);
  assert(std::abs(solver.best_distance() - 6200) < 1);
  const float expectedGrazing[] = {1, 1, 0, 1, .5f, .75f, 0};
  previous = pilot;
  for (size_t i = 0; i < n; ++i) {
    const auto adjusted =
        conservativePoint(solver.intercept(i), previous, route[i], MarginPolicy::Angle0);
    assert(std::abs(adjusted.grazing - expectedGrazing[i]) < .04);
    previous = solver.intercept(i);
  }
  std::cout << "Margin extremes, mixed-angle route, small cylinders, interior/zero vectors, path "
               "consistency and "
               "bearing checks passed\n";
}
