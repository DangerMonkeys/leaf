#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include "math/kalman.h"

void fatalErrorInfo(const char*, ...) {}
void fatalError(const char*, ...) { throw std::runtime_error("Kalman fatal error"); }

int main() {
  // Exercise both covariance layouts repeatedly in the same object.
  KalmanFilterPA filter(.01, .09);
  for (bool bias : {true, false, true, false}) {
    for (int gapMs : {200, 250, 500, 1000, 1500, 1501}) {
      filter.reset(25, bias);
      const double offset = bias ? -.1 : 0;
      for (int i = 0; i <= 1200; ++i) {
        const double t = i * .05;
        filter.update(t, 100 + 2 * t, offset);
      }
      assert(std::abs(filter.getVelocity() - 2) < .01);
      const double learned = filter.getAccelerationBias();
      const double nextTime = 60 + gapMs * .001;
      filter.update(nextTime, 100 + 2 * nextTime, offset, false);
      if (gapMs <= 1500) {
        assert(std::abs(filter.getVelocity() - 2) < .02);
        assert(filter.getAccelerationBias() == learned);
      } else {
        assert(filter.getVelocity() == 0);
        assert(filter.getAccelerationBias() == 0);
      }
      const double position = filter.getPosition(), velocity = filter.getVelocity();
      filter.update(nextTime, 999, 99);
      filter.update(nextTime - .1, 999, 99);
      assert(filter.getPosition() == position && filter.getVelocity() == velocity);
    }
  }
  std::printf(
      "Kalman gap boundaries, bias preservation and covariance mode switching passed; size=%zu\n",
      sizeof(KalmanFilterPA));
}
