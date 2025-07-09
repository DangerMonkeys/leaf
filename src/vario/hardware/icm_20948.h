#pragma once

#include "hardware/motion_source.h"

#include <ICM_20948.h>

class ICM20948 : public IMotionSource {
 public:
  void init();
  MotionUpdateResult update();
  void getOrientation(unsigned long* t, double* qx, double* qy, double* qz);
  void getAcceleration(unsigned long* t, double* ax, double* ay, double* az);

 private:
  ICM_20948_I2C IMU_;
  unsigned long tQuaternion_;
  unsigned long tAcceleration_;
  double qx_, qy_, qz_;
  double ax_, ay_, az_;
};
