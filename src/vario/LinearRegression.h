#ifndef LinearRegression_h
#define LinearRegression_h
struct LinearFit {
  double x0;
  double m;
  double b;
};

double linear_value(const struct LinearFit* fit, double x) { return fit->m * x + fit->b; }

double linear_derivative(const struct LinearFit* fit, double x) { return fit->m; }

template <unsigned int n>
class LinearRegression {
 public:
  LinearRegression();
  void reset();
  void update(double x, double y);
  LinearFit fit();
  double max_deviation_y();
  double most_recent_y();

 private:
  unsigned int _index;
  unsigned int _count;
  double _Xi[n];
  double _Yi[n];
  double _sumXiYi;
  double _sumXi;
  double _sumYi;
  double _sumXi2;
};

template <unsigned int n>
void LinearRegression<n>::reset() {
  for (int i = 0; i < n; i++) {
    _Xi[i] = 0;
    _Yi[i] = 0;
  }
  _index = 0;
  _sumXiYi = 0;
  _sumXi = 0;
  _sumYi = 0;
  _sumXi2 = 0;
}

template <unsigned int n>
LinearRegression<n>::LinearRegression() {
  reset();
}

template <unsigned int n>
void LinearRegression<n>::update(double x, double y) {
  // Remove current data point
  // _sumXiYi -= _Xi[_index] * _Yi[_index];
  _sumXi -= _Xi[_index];
  _sumYi -= _Yi[_index];
  // _sumXi2 -= _Xi[_index] * _Xi[_index];

  // Add new data point
  _Xi[_index] = x;
  _Yi[_index] = y;
  // _sumXiYi += x * y;
  _sumXi += x;
  _sumYi += y;
  // _sumXi2 += x * x;
  _index += 1;
  if (_index >= n) {
    _index = 0;
  }

  _count++;
  if (_count >= n) {
    _count = n;
  }
}

template <unsigned int n>
LinearFit LinearRegression<n>::fit() {
  LinearFit result;
  if (_count == 0) {
    result.m = 0;
    result.b = 0;
  } else if (_count == 1) {
    result.m = 0;
    result.b = _Yi[0];
  } else {
    double x0 = _Xi[0];
    double y0 = _Yi[0];
    double sum_xi = 0.0;
    double sum_xi2 = 0.0;
    double sum_yi = 0.0;
    double sum_xiyi = 0.0;
    for (uint8_t i = 0; i < _count; i++) {
      double dx = _Xi[i] - x0;
      double dy = _Yi[i] - y0;
      sum_xi += dx;
      sum_xi2 += dx * dx;
      sum_yi += dy;
      sum_xiyi += dx * dy;
    }
    double denominator = _count * sum_xi2 - sum_xi * sum_xi;
    double numerator = _count * sum_xiyi - sum_xi * sum_yi;

    if (denominator == 0) {
      Serial.println("OH NO DENOMINATOR IS 0!!!");
      Serial.print("_count=");
      Serial.print(_count);
      Serial.print(", _sumXi2=");
      Serial.print(_sumXi2);
      Serial.print(", _sumXi=");
      Serial.print(_sumXi);
      Serial.println();
      Serial.print("_Xi = {");
      for (uint8_t i = 0; i < _count; i++) {
        Serial.print(_Xi[i]);
        Serial.print(", ");
      }
      Serial.println("}");
      Serial.print("_Yi = {");
      for (uint8_t i = 0; i < _count; i++) {
        Serial.print(_Yi[i]);
        Serial.print(", ");
      }
      Serial.println("}");
      // die();
      while (true) {
        delay(1);
      }
      // TODO: safer end state here
    }
    result.m = numerator / denominator;
    result.b = (_sumYi - result.m * _sumXi) / _count;
  }
  return result;
}

template <unsigned int n>
double LinearRegression<n>::max_deviation_y() {
  double y_mean = _sumYi / _count;
  double max_dev = 0;
  for (uint8_t i = 0; i < n; i++) {
    double new_dev = abs(_Yi[i] - y_mean);
    if (new_dev > max_dev) max_dev = new_dev;
  }
  return max_dev;
}

template <unsigned int n>
double LinearRegression<n>::most_recent_y() {
  int i = _index;
  i--;
  if (i < 0) {
    i = n - 1;
  }
  return _Yi[i];
}
#endif