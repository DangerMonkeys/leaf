#pragma once

// Kalman filter for position and acceleration
class KalmanFilterPA {
 public:
  KalmanFilterPA(double positionVariance, double accelerationVariance)
      : pVar_(positionVariance), aVar_(accelerationVariance) {}

  void init(double initialTime, double initialPosition, double initialAcceleration);

  void update(double measuredTime, double measuredPosition, double measuredAcceleration);

  double getPosition() { return p_; }
  double getVelocity() { return v_; }
  double getAcceleration() { return a_; }

 private:
  // Position variance
  const double pVar_;

  // Acceleration variance
  const double aVar_;

  // Current time
  double t_;

  // Current position
  double p_;

  // Current velocity
  double v_;

  // Current acceleration
  double a_;

  // Covariance matrix
  double p11_, p21_, p12_, p22_;
};
