#pragma once

#include <stdint.h>

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "dispatch/message_injector.h"

namespace sim {
  struct FreeFlightParameters {
    double windMps = 2;
    double windFromDeg = 250;
    double diameterM = 160;
    double strengthMps = 4;
    double peakiness = 1.65;
    double diameterVariance = 0;
    double strengthVariance = 0;
    double texture = 0;
    double tiltDeg = 26.565;
    double sinkMps = 1;
    bool drifting = false;
    uint32_t seed = 1;
  };

  struct FreeFlightPoint {
    double x = 0, y = 0, z = 1000, climb = 0, t = 0;
  };

  // Independent ground-truth environment. Never uses the breadcrumb estimator's projection.
  struct FreeFlightModel {
    FreeFlightParameters parameters;
    double x = 0, y = -120, z = 1000, headingDeg = 0, turnRateDps = 0;
    double airspeedMps = 9.5, climbMps = 0, liftMps = 0, elapsedS = 0;
    double sourceAltM = 1000;
    void reset(double altitudeM = 1000);
    void step(double dt, double steering, double speedInput);
    void profile(double altitudeM, double& centerX, double& centerY, double& radiusM,
                 double& strengthMps) const;
    double liftAt(double eastM, double northM, double altitudeM) const;
  };

  struct FreeFlightSnapshot {
    bool active = false;
    FreeFlightModel model;
    std::vector<FreeFlightPoint> trail;
    double centerX = 0, centerY = 0, radiusM = 80, strengthMps = 4;
  };

  class FreeFlight {
   public:
    // start/stop/update run on the device thread. Settings and controls may come from HTTP.
    void start(uint32_t nowMs, etl::imessage_bus* bus, double altitudeM = 1000);
    void stop();
    void update(uint32_t nowMs);
    void configure(const FreeFlightParameters& parameters);
    void controls(double steering, double speedInput, bool straighten = false);
    FreeFlightSnapshot snapshot(bool includeTrail = true) const;
    std::string recording() const;

   private:
    mutable std::mutex mutex_;
    bool active_ = false;
    FreeFlightModel model_;
    MessageInjector injector_;
    uint32_t lastStepMs_ = 0, steps_ = 0;
    double steering_ = 0, speedInput_ = 0;
    uint64_t controlAtUs_ = 0;
    std::deque<FreeFlightPoint> trail_;
    std::deque<std::string> log_;
  };

  FreeFlight& freeFlight();
}  // namespace sim
