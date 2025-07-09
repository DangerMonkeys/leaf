#pragma once

#include "utils/flags_enum.h"

// Resulting state change(s) for an update to an ISpatialMotionSource
DEFINE_FLAGS_ENUM(MotionUpdateResult, uint8_t){
    None = 0,
    NoChange = 1 << 0,
    QuaternionReady = 1 << 1,
    AccelerationReady = 1 << 2,
};

class IMotionSource {
 public:
  // Initialize the motion source.  Before this method is called, none of the other
  // methods are expected to behave correctly.  After this method completes, all of
  // the other methods are expected to behave correctly.
  virtual void init() = 0;

  // Call this method as frequently as desired to perform motion acquisition tasks.
  // The return result indicates when new motion information has been acquired.
  virtual MotionUpdateResult update() = 0;

  // Get orientation quaternion
  virtual void getOrientation(unsigned long* t, double* qx, double* qy, double* qz) = 0;

  // Get acceleration vector
  virtual void getAcceleration(unsigned long* t, double* ax, double* ay, double* az) = 0;

  virtual ~IMotionSource() = default;  // Always provide a virtual destructor
};
