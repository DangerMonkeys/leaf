#pragma once

// Kalman filter for position and acceleration
class KalmanFilterPA {
 public:
  KalmanFilterPA(double positionVariance, double accelerationVariance)
      : pVar_(positionVariance), aVar_(accelerationVariance) {}

  void update(double measuredTime, double measuredPosition, double measuredAcceleration);

  bool initialized() { return initialized_; }
  double getPosition();
  double getVelocity();
  double getAcceleration();

 private:
  void init(double initialTime, double initialPosition, double initialAcceleration);

  bool initialized_ = false;

  // Position variance
  const double pVar_;

  // Acceleration variance
  const double aVar_;

  // Current time
  double t_;

  // Current position
  double p_ = 0;

  // Current velocity
  double v_ = 0;

  // Current acceleration
  double a_ = 0;

  // Covariance matrix
  double p11_, p21_, p12_, p22_;
};
