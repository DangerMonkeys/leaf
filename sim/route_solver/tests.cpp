#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <random>
#include "solver.h"

static bool track_allocations = false;
static unsigned allocations = 0;
void* operator new(std::size_t n) {
  if (track_allocations) ++allocations;
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
using namespace route_experiment;

template <template <size_t> class Solver>
void checks() {
  Solver<10> solver;
  Disk one[]{{{1000, 0}, 100}};
  solver.initialize(one, 1, {0, 0});
  solver.improve(256);
  assert(std::abs(solver.best_distance() - 900) < 0.1);
  solver.update_position({1000, 0});
  solver.improve(1);
  assert(solver.best_distance() < 0.1);
  // Reinitializing the same object must discard its previous optimum.
  solver.initialize(one, 1, {0, 0});
  solver.improve(256);
  assert(std::abs(solver.best_distance() - 900) < 0.1);

  Disk line[]{{{1000, 0}, 400}, {{2000, 0}, 400}, {{3000, 0}, 400}};
  solver.initialize(line, 3, {0, 0});
  solver.improve(2048);
  assert(std::abs(solver.best_distance() - 2600) < 0.2);
  solver.update_position({1100, 0});
  solver.advance_waypoint();
  solver.improve(512);
  assert(std::abs(solver.best_distance() - 1500) < 0.2);

  std::mt19937 gen(42);
  std::uniform_real_distribution<float> xy(-20000, 20000), radius(0, 5000);
  for (unsigned trial = 0; trial < 100; ++trial) {
    Disk disks[10];
    for (auto& d : disks) d = {{xy(gen), xy(gen)}, radius(gen)};
    solver.initialize(disks, 10, {0, 0});
    float previous = solver.best_distance();
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
      solver.improve(1);
      assert(solver.best_distance() <= previous + 0.02f);
      previous = solver.best_distance();
      for (unsigned i = 0; i < 10; ++i) {
        Vec delta = solver.intercept(i) - disks[i].center;
        assert(std::hypot(delta.x, delta.y) <= disks[i].radius + 0.02f);
      }
    }
    track_allocations = true;
    solver.update_position({250, 500});
    solver.improve(4, 6);
    solver.advance_waypoint();
    track_allocations = false;
    assert(allocations == 0);
  }
}
int main() {
  checks<Relaxation>();
  checks<PrimalDual>();
  PrimalDual<10> solver;
  Disk disks[]{{{1000, 1000}, 400}, {{2000, 0}, 400}};
  solver.initialize(disks, 2, {0, 0});
  solver.improve(4096);
  assert(solver.lower_bound() <= solver.best_distance() + 0.05);
  assert(solver.best_distance() - solver.lower_bound() < 0.1);
  std::cout << "Geometry, feasibility, reuse, advancement, monotonic best, dual and allocation "
               "checks passed\n";
}
