#include "math/kalman.h"

#include <Arduino.h>

#include "diagnostics/fatal_error.h"

void KalmanFilterPA::init(double initialTime, double initialPosition, double initialAcceleration) {
  t_ = initialTime;
  p_ = initialPosition;
  v_ = 0;
  a_ = initialAcceleration;

  p11_ = 0;
  p12_ = 0;
  p21_ = 0;
  p22_ = 0;

  initialized_ = true;
}

double KalmanFilterPA::getPosition() {
  if (!initialized_) fatalError("KalmanFilter::getPosition before initialized");
  return p_;
}
double KalmanFilterPA::getVelocity() {
  if (!initialized_) fatalError("KalmanFilter::getVelocity before initialized");
  return v_;
}
double KalmanFilterPA::getAcceleration() {
  if (!initialized_) fatalError("KalmanFilter::getAcceleration before initialized");
  return a_;
}

void KalmanFilterPA::update(double measuredTime, double measuredPosition,
                            double measuredAcceleration) {
  if (isnan(measuredTime) || isinf(measuredTime) || isnan(measuredPosition) ||
      isinf(measuredPosition) || isnan(measuredAcceleration) || isinf(measuredAcceleration)) {
    fatalErrorInfo("measuredTime=%g, measuredPosition=%g, measuredAcceleration=%g", measuredTime,
                   measuredPosition, measuredAcceleration);
    fatalError("input value to KalmanFilterPA::update was invalid");
  }

  if (!initialized_) {
    init(measuredTime, measuredPosition, measuredAcceleration);
    return;
  }

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
  if (isnan(dp) || isinf(dp)) {
    fatalErrorInfo("dp=%g, measuredPosition=%g, p_=%g", dp, measuredPosition, p_);
    fatalError("Kalman dp invalid");
  }
  if (isnan(k11) || isinf(k11) || isnan(k12) || isinf(k12)) {
    fatalErrorInfo("measuredTime=%g, measuredPosition=%g, measuredAcceleration=%g", measuredTime,
                   measuredPosition, measuredAcceleration);
    fatalErrorInfo("p11_=%g, p21_=%g, p12_=%g, p22_=%g", p11_, p21_, p12_, p22_);
    fatalErrorInfo("s=%g, pVar_=%g, aVar_=", s, pVar_, aVar_);
    fatalErrorInfo("k11=%g, k12=%g", k11, k12);
    fatalError("Kalman variable was invalid in KalmanFilterPA::update");
  }

  // Update
  p_ += k11 * dp;
  if (isnan(p_) || isinf(p_)) {
    fatalErrorInfo("p_=%g, k11=%g, dp=%g", p_, k11, dp);
    fatalError("Kalman p_ invalid");
  }
  v_ += k12 * dp;
  if (isnan(v_) || isinf(v_)) {
    fatalErrorInfo("v_=%g, k12=%g, dp=%g", v_, k12, dp);
    fatalError("Kalman v_ invalid");
  }
  p22_ -= k12 * p21_;
  p12_ -= k12 * p11_;
  p21_ -= k11 * p21_;
  p11_ -= k11 * p11_;
}
