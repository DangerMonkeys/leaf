#include "math/gravity_projection.h"
#include <cassert>
#include <initializer_list>
#include <limits>

int main() {
  double v, g;
  // At rest on each of six faces, gravity cancels, independent of mounting.
  for (int axis = 0; axis < 3; ++axis) for (int sign : {-1, 1}) {
    double a[3]{}; a[axis] = sign;
    assert(projectVerticalAcceleration(a[0], a[1], a[2], a[0], a[1], a[2], v, g));
    assert(fabs(v) < 1e-12 && fabs(g - 1) < 1e-12);
    // 0.2g upward acceleration has the same sign even with an inverted device.
    assert(projectVerticalAcceleration(a[0]*1.2, a[1]*1.2, a[2]*1.2, a[0], a[1], a[2], v, g));
    assert(fabs(v - .2) < 1e-12);
  }
  // Arbitrary tilt, horizontal acceleration, and simultaneous sink acceleration.
  const double c = sqrt(.5);
  assert(projectVerticalAcceleration(.8*c + .3*c, .8*c - .3*c, 0, c, c, 0, v, g));
  assert(fabs(v + .2) < 1e-12);
  // Freefall remains a real measurement; never replace it with zero acceleration.
  assert(projectVerticalAcceleration(0, 0, 0, 0, 0, 1, v, g) && v == -1);
  assert(!projectVerticalAcceleration(0, 0, 1, 0, 0, 0, v, g));
  assert(!projectVerticalAcceleration(0, 0, 1, 0, 0, 2, v, g));
  // High acceleration inside the operating range must not reset validity.
  assert(projectVerticalAcceleration(4, 0, 1, 0, 0, 1, v, g) && fabs(v) < 1e-12);
  assert(projectVerticalAcceleration(0, 0, 6, 0, 0, 1, v, g) && v == 5);
  assert(projectVerticalAcceleration(0, 0, -6, 0, 0, 1, v, g) && v == -7);
  assert(!projectVerticalAcceleration(8, 0, 1, 0, 0, 1, v, g));
  assert(!projectVerticalAcceleration(NAN, 0, 1, 0, 0, 1, v, g));
  assert(!projectVerticalAcceleration(0, 0, 1, 0, INFINITY, 1, v, g));
}
