#pragma once

// Compile-time settings determining which information should be sent to the message bus as comments
// (which can then be written to the bus log if enabled)
namespace LOG {
  // IMU instrument Kalman filter state upon every update
  // Format: kalman,<position>,<velocity>,<acceleration>
  constexpr bool KALMAN = true;

  // GPS instrument state upon every call to update() (separate from incoming NMEA messages)
  // Format: gps,<latitude>,<longitude>,<altitude in m>,<speed in m/s>,<course in degrees>
  constexpr bool GPS = true;

  // Barometer instrument instantaneous pressure upon every update (via message from message bus)
  // Format: baro mb*100,<pressure in 100*millibars>
  constexpr bool BARO = true;
}  // namespace LOG
