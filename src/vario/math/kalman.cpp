#include "math/kalman.h"

void KalmanFilterPA::init(double initialTime, double initialPosition, double initialAcceleration) {
  t_ = initialTime;
  p_ = initialPosition;
  v_ = 0;
  a_ = initialAcceleration;

  p11_ = 0;
  p12_ = 0;
  p21_ = 0;
  p22_ = 0;
}

void KalmanFilterPA::update(double measuredTime, double measuredPosition,
                            double measuredAcceleration) {
  double dt = measuredTime - t_;
  double dt2 = dt * dt;
  double dt3 = dt2 * dt;
  double dt4 = dt3 * dt;
  t_ = measuredTime;

  // Prediction
  a_ = measuredAcceleration;
  p_ += dt * v_ + dt2 * a_ / 2;
  v_ += dt * a_;

  // Covariance update
  double inc = dt * p22_ + dt3 * aVar_ / 2;
  p11_ += dt * (p12_ + p21_ + inc) - (dt4 * aVar_ / 4);
  p21_ += inc;
  p12_ += inc;
  p22_ += dt2 * aVar_;

  // Gain
  double s = p11_ + pVar_;
  double k11 = p11_ / s;
  double k12 = p12_ / s;
  double dp = measuredPosition - p_;

  // Update
  p_ += k11 * dp;
  v_ += k12 * dp;
  p22_ -= k12 * p21_;
  p12_ -= k12 * p11_;
  p21_ -= k11 * p21_;
  p11_ -= k11 * p11_;
}
