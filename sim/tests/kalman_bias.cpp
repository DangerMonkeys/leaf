#include "math/kalman.h"
#include <cassert>
#include <cmath>
#include <stdexcept>

void fatalErrorInfo(const char*, ...) {}
void fatalError(const char*, ...) { throw std::runtime_error("Kalman fatal error"); }

int main() {
  KalmanFilterPA filter(.01, .09);
  filter.reset(25, true);
  const double dt = .05, offset = -.1;
  double t = 0, p = 100, v = 0;
  for (int i = 0; i < 1200; ++i, t += dt) filter.update(t, p, offset);
  const double learned = filter.getAccelerationBias();
  assert(fabs(learned - offset) < .002);
  double peakVelocity = 0;
  for (int i = 0; i < 100; ++i, t += dt) {
    const double a = i < 20 ? 49.03325 : (i < 40 ? -49.03325 : 0);
    p += v * dt + .5 * a * dt * dt;
    v += a * dt;
    // Motion/pressure disturbances must not alter the held correction, but must still
    // change predicted climb. The following three seconds also exercise recovery holdoff.
    filter.update(t, p + (i < 40 ? .5 * sin(i) : 0), a + offset, false);
    assert(filter.getAccelerationBias() == learned);
    peakVelocity = fmax(peakVelocity, fabs(filter.getVelocity()));
  }
  assert(peakVelocity > 10);
  assert(fabs(filter.getVelocity()) < .1);
  for (int i = 0; i < 400; ++i, t += dt) filter.update(t, p, offset, true);
  assert(fabs(filter.getAccelerationBias() - offset) < .01);
  assert(fabs(filter.getVelocity()) < .02);
}
