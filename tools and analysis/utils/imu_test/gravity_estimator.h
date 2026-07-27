#pragma once

// Estimator of constant gravity acceleration.
class GravityEstimator {
 public:
  // Create an estimator incorporating new vertical acceleration measurements at a specified rate.
  // newMeasurementWeight: Weight given to new measurements (0, 1).
  // afterSeconds: Number of seconds after which newMeasurementWeight is given to new measurements.
  GravityEstimator(
    double newMeasurementWeight, double afterSeconds)
      : kUpdate_(log(1 - newMeasurementWeight) / afterSeconds)
  {}

  // Initialize estimator.
  // t: Current time (milliseconds).
  void init(uint32_t t) {
    tPrev_ = t;
    value_ = 1.0;
  }

  // Provide new vertical acceleration measurement.
  // t: Time measurement was taken (milliseconds).
  // value: Measured vertical acceleration.
  void update(uint32_t t, double value) {
    double dt = ((double)t - tPrev_) * 0.001;
    double f = exp(kUpdate_ * dt);
    tPrev_ = t;
    value_ = value_ * f + value * (1 - f);
  }

  inline double estimate() {
    return value_;
  }

 private:
  // Last time gravity estimate was updated
  uint32_t tPrev_;

  // Rate at which new measurements are incorporated into best guess
  double kUpdate_;

  // Best guess for strength of gravity
  double value_;
};
