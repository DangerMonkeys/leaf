#pragma once

constexpr double constexpr_log(double x) {
  // Simple Newton-Raphson for ln(x)
  double y = 0.0;
  for (int i = 0; i < 50; i++) {
    double e = 1.0;
    // compute exp(y) approximately
    double term = 1.0;
    for (int j = 1; j < 20; j++) {
      term *= y / j;
      e += term;
    }
    y -= (e - x) / e;
  }
  return y;
}
