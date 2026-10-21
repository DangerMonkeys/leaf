#include "math/kalman.h"

#include <Arduino.h>

#include "diagnostics/fatal_error.h"

void KalmanFilterPA::init(double initialTime, double initialPosition, double initialAcceleration) {
  t_ = initialTime;
  p_ = initialPosition;
  v_ = 0;
  a_ = initialAcceleration;

  p11_ = initialVelocityVariance_ > 0 ? pVar_ : 0;
  p12_ = 0;
  p21_ = 0;
  p22_ = initialVelocityVariance_;
  bias_ = estimateAccelerationBias_ ? initialAccelerationBias_ : 0;
  a_ = initialAcceleration - bias_;
  for (auto& row : covariance_)
    for (double& value : row) value = 0;
  covariance_[0][0] = pVar_;
  covariance_[1][1] = initialVelocityVariance_;
  // 25 mg initial bias uncertainty covers the observed ~10 mg board residual.
  covariance_[2][2] = .25 * .25;

  initialized_ = true;
}

double KalmanFilterPA::getPosition() const {
  if (!initialized_) fatalError("KalmanFilter::getPosition before initialized");
  return p_;
}
double KalmanFilterPA::getVelocity() const {
  if (!initialized_) fatalError("KalmanFilter::getVelocity before initialized");
  return v_;
}
double KalmanFilterPA::getAcceleration() const {
  if (!initialized_) fatalError("KalmanFilter::getAcceleration before initialized");
  return a_;
}

void KalmanFilterPA::update(double measuredTime, double measuredPosition,
                            double measuredAcceleration, bool allowBiasLearning) {
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
  if (dt <= 0.0) {
    return;
  }
  if (dt > 1.0) {
    init(measuredTime, measuredPosition, measuredAcceleration);
    return;
  }

  double dt2 = dt * dt;
  double dt3 = dt2 * dt;
  double dt4 = dt3 * dt;
  t_ = measuredTime;

  if (estimateAccelerationBias_) {
    updateWithBias(dt, measuredPosition, measuredAcceleration, allowBiasLearning);
    return;
  }

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

void KalmanFilterPA::updateWithBias(double dt, double measuredPosition, double measuredAcceleration,
                                    bool allowBiasLearning) {
  // State [altitude, climb, vertical acceleration bias]. Gravity direction remains entirely
  // SFLP's responsibility. Bias is observable from barometric motion, including during flight;
  // it is never inferred by assuming the device's measured acceleration ought to be zero.
  const double halfDt2 = .5 * dt * dt;
  const double f[3][3] = {{1, dt, -halfDt2}, {0, 1, -dt}, {0, 0, 1}};
  const double b[3] = {halfDt2, dt, 0};
  a_ = measuredAcceleration - bias_;
  p_ += dt * v_ + halfDt2 * a_;
  v_ += dt * a_;

  double predicted[3][3]{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      for (int k = 0; k < 3; ++k)
        for (int l = 0; l < 3; ++l) predicted[i][j] += f[i][k] * covariance_[k][l] * f[j][l];
      predicted[i][j] += b[i] * b[j] * aVar_;
    }
  // Slow bias random walk (0.005 m/s^2 / sqrt(s)); does not replace maneuver acceleration.
  predicted[2][2] += .005 * .005 * dt;
  const double innovationVariance = predicted[0][0] + pVar_;
  double gain[3];
  for (int i = 0; i < 3; ++i) gain[i] = predicted[i][0] / innovationVariance;
  // Keep maneuver/pressure transients out of the slowly learned sensor correction. Prediction
  // and barometric position/velocity correction continue using the measured acceleration.
  // Use this constrained gain in Joseph's update too, preserving covariance consistency.
  if (!allowBiasLearning) gain[2] = 0;
  const double residual = measuredPosition - p_;
  p_ += gain[0] * residual;
  v_ += gain[1] * residual;
  bias_ += gain[2] * residual;
  a_ = measuredAcceleration - bias_;

  // Joseph form preserves covariance symmetry/positive semidefiniteness under roundoff.
  double correction[3][3] = {{1 - gain[0], 0, 0}, {-gain[1], 1, 0}, {-gain[2], 0, 1}};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      covariance_[i][j] = gain[i] * pVar_ * gain[j];
      for (int k = 0; k < 3; ++k)
        for (int l = 0; l < 3; ++l)
          covariance_[i][j] += correction[i][k] * predicted[k][l] * correction[j][l];
    }
}
