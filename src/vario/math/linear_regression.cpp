#include "linear_regression.h"

double linear_value(const struct LinearFit* fit, double x) { return fit->m * x + fit->b; }

double linear_derivative(const struct LinearFit* fit, double x) { return fit->m; }
