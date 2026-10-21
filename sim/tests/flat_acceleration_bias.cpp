#include "math/flat_acceleration_bias.h"
#include "math/kalman.h"
#include <cassert>
#include <cmath>
#include <stdexcept>

void fatalErrorInfo(const char*, ...) {}
void fatalError(const char*, ...) { throw std::runtime_error("Kalman fatal error"); }

int main() {
  FlatAccelerationBias reference{FlatAccelerationBias::VERSION, -.01f, 0, 0, 1};
  assert(reference.valid());
  const double seed = reference.initialBias(0, 0, 1);
  assert(fabs(seed + .0980665) < 1e-7);
  const double tilted = reference.initialBias(0, sqrt(.5), sqrt(.5));
  assert(fabs(tilted - seed * sqrt(.5)) < 1e-7);
  assert(reference.initialBias(1, 0, 0) == 0);
  assert(reference.initialBias(0, 0, -1) == 0);
  assert(reference.initialBias(0, 0, NAN) == 0);
  auto corrupt = reference;
  corrupt.version++;
  assert(corrupt.initialBias(0, 0, 1) == 0);
  corrupt = reference;
  corrupt.residualG = NAN;
  assert(!corrupt.valid());
  corrupt = reference;
  corrupt.gz = -1;
  assert(!corrupt.valid());

  KalmanFilterPA filter(.01, .09);
  filter.reset(25, true, seed);
  filter.update(0, 100, seed);
  assert(filter.getAccelerationBias() == seed);
  assert(fabs(filter.getAcceleration()) < 1e-9);
  // A seed is not a locked correction: learn a different true bias while moving.
  const double actualBias = -.06, acceleration = .1;
  for (int i = 1; i <= 1200; ++i) {
    const double t = i * .05;
    filter.update(t, 100 + .5 * acceleration * t * t, acceleration + actualBias);
  }
  assert(fabs(filter.getAccelerationBias() - actualBias) < .002);
  assert(fabs(filter.getVelocity() - 6) < .02);
  filter.reset(0, false, seed);
  filter.update(0, 100, 0);
  assert(filter.getAccelerationBias() == 0);  // ICM behavior remains unchanged.
}
