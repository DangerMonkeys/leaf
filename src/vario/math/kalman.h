#pragma once

// Kalman filter for position and acceleration
class KalmanFilterPA {
 public:
  KalmanFilterPA(double positionVariance, double accelerationVariance)
      : pVar_(positionVariance), aVar_(accelerationVariance) {}

  void update(double measuredTime, double measuredPosition, double measuredAcceleration);

  bool initialized() const { return initialized_; }
  // A moving start needs uncertainty in initial velocity, not a confident zero-velocity prior.
  // Default preserves the historical ICM initialization.
  void reset(double initialVelocityVariance = 0, bool estimateAccelerationBias = false) {
    initialized_ = false;
    bias_ = 0;
    initialVelocityVariance_ = initialVelocityVariance;
    estimateAccelerationBias_ = estimateAccelerationBias;
  }
  double getAccelerationBias() const { return estimateAccelerationBias_ ? bias_ : 0; }
  double getPosition() const;
  double getVelocity() const;
  double getAcceleration() const;

 private:
  void init(double initialTime, double initialPosition, double initialAcceleration);
  void updateWithBias(double dt, double measuredPosition, double measuredAcceleration);

  bool initialized_ = false;
  double initialVelocityVariance_ = 0;
  bool estimateAccelerationBias_ = false;
  double bias_ = 0;
  double covariance_[3][3]{};

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
