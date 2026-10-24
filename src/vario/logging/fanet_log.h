#pragma once

#include <stdint.h>

// Independent of the bus logger. Lifecycle and SD writes belong to the main flight task;
// the radio receive task only submits fixed-size observations without waiting.
namespace fanet_log {
  struct Observation {
    uint32_t receivedMs = 0;
    uint32_t neighborId = 0;
    float distanceKm = 0;
    float rssi = 0;
    float snr = 0;
    double neighborLat = 0;
    double neighborLon = 0;
    double receiverLat = 0;
    double receiverLon = 0;
    bool receiverFix = false;
  };

  bool recording();
  void record(const Observation& observation);
  void beginFlight();
  void endFlight();
  bool update();
}  // namespace fanet_log
