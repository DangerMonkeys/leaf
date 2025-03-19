#pragma once

#include "utils/flags_enum.h"

// Resulting state change(s) for an update to an IPressureSource
DEFINE_FLAGS_ENUM(PressureUpdateResult, uint8_t){
    None = 0,
    NoChange = 1 << 0,
    PressureReady = 1 << 1,
};

class IPressureSource {
 public:
  // Initialize the pressure source.  Before this method is called, none of the other
  // methods are expected to behave correctly.  After this method completes, all of
  // the other methods are expected to behave correctly.
  virtual void init() = 0;

  // Call this method as frequently as desired to perform pressure acquisition tasks.
  // The return result indicates when new pressure information has been acquired.
  virtual PressureUpdateResult update() = 0;

  // Inform the pressure source that new pressure information is desired.  After
  // calling this method, call `update` until the pressure information is obtained.
  virtual void startMeasurement() = 0;

  // setting to control when to update temp reading from sensor (we can do
  // temp every ~1 sec, even though we're doing pressure every 50ms)
  // TODO: specify temp measurement every X often instead
  virtual void enableTemp(bool enable) = 0;

  // Get pressure in 1/100th mBars
  virtual int32_t getPressure() = 0;

  virtual ~IPressureSource() = default;  // Always provide a virtual destructor
};
