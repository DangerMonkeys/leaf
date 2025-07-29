#pragma once

#pragma once

#include "utils/flags_enum.h"

// Resulting state change(s) for an update to an IAmbientSource
DEFINE_FLAGS_ENUM(AmbientUpdateResult, uint8_t){
    None = 0,
    NoChange = 1 << 0,
    TemperatureReady = 1 << 1,
    RelativeHumidityReady = 1 << 2,
};

class IAmbientSource {
 public:
  // Call this method as frequently as desired to perform ambient environmental acquisition tasks.
  // The return result indicates when new environmental information has been acquired.
  virtual AmbientUpdateResult update() = 0;

  // Get the most recent temperature in degrees Celsius
  virtual float getTemp() = 0;

  // Get the most recent relative humidity in percent
  virtual float getHumidity() = 0;

  virtual ~IAmbientSource() = default;  // Always provide a virtual destructor
};
