#include "free_flight.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "scenario.h"
#include "sim/clock.h"

namespace sim {
  namespace {
    constexpr double RAD = 3.141592653589793 / 180;
    double bounded(double value, double lo, double hi, double fallback) {
      return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
    }
  }  // namespace

  void FreeFlightModel::reset(double altitudeM) {
    elapsedS = 0;
    x = -parameters.diameterM * .12;
    y = -parameters.diameterM * .75;
    z = sourceAltM = altitudeM;
    headingDeg = turnRateDps = climbMps = liftMps = 0;
    airspeedMps = 9.5;
    liftMps = liftAt(x, y, z);
    climbMps = liftMps - parameters.sinkMps;
  }

  void FreeFlightModel::profile(double altitudeM, double& centerX, double& centerY, double& radiusM,
                                double& strengthMps) const {
    const auto& p = parameters;
    const double phase = (p.seed % 997) * .037;
    const double height = altitudeM - sourceAltM;
    const double direction = (p.windFromDeg + 180) * RAD;
    const double offset =
        height * std::tan(p.tiltDeg * RAD) + (p.drifting ? p.windMps * elapsedS : 0);
    centerX = std::sin(direction) * offset;
    centerY = std::cos(direction) * offset;
    radiusM = p.diameterM * .5 * (1 + p.diameterVariance * std::sin(height / 110 + phase));
    strengthMps = p.strengthMps * (1 + p.strengthVariance * std::sin(height / 130 + phase + 1));
  }

  double FreeFlightModel::liftAt(double eastM, double northM, double altitudeM) const {
    double cx, cy, radius, strength;
    profile(altitudeM, cx, cy, radius, strength);
    const double normalizedR = std::hypot(eastM - cx, northM - cy) / radius;
    const double core = strength * std::exp(-parameters.peakiness * normalizedR * normalizedR);
    const double texture = 1 + parameters.texture * .25 *
                                   std::sin(eastM * .09 + elapsedS * .12 + parameters.seed) *
                                   std::cos(northM * .07);
    return core * texture - .4 * std::clamp(normalizedR - 1, 0.0, 1.0);
  }

  void FreeFlightModel::step(double dt, double steering, double speedInput) {
    turnRateDps = std::clamp(turnRateDps + steering * 14 * dt, -30.0, 30.0);
    airspeedMps = std::clamp(airspeedMps + speedInput * 2 * dt, 6.0, 15.0);
    // Midpoint integration keeps constant turns smooth at the 20 Hz sensor rate.
    const double midHeading = (headingDeg + turnRateDps * dt * .5) * RAD;
    headingDeg = std::fmod(headingDeg + turnRateDps * dt + 360, 360);
    const double windTo = (parameters.windFromDeg + 180) * RAD;
    x += (std::sin(midHeading) * airspeedMps + std::sin(windTo) * parameters.windMps) * dt;
    y += (std::cos(midHeading) * airspeedMps + std::cos(windTo) * parameters.windMps) * dt;
    liftMps = liftAt(x, y, z);
    climbMps = liftMps - parameters.sinkMps;
    z += climbMps * dt;
    elapsedS += dt;
  }

  void FreeFlight::configure(const FreeFlightParameters& p) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& q = model_.parameters;
    q.windMps = bounded(p.windMps, 0, 15, 2);
    q.windFromDeg = bounded(p.windFromDeg, 0, 360, 250);
    q.diameterM = bounded(p.diameterM, 40, 500, 160);
    q.strengthMps = bounded(p.strengthMps, .5, 10, 4);
    q.peakiness = bounded(p.peakiness, .3, 5, 1.65);
    q.diameterVariance = bounded(p.diameterVariance, 0, .7, 0);
    q.strengthVariance = bounded(p.strengthVariance, 0, .7, 0);
    q.texture = bounded(p.texture, 0, 1, 0);
    q.tiltDeg = bounded(p.tiltDeg, 0, 70, 26.565);
    q.sinkMps = bounded(p.sinkMps, .5, 2, 1);
    q.drifting = p.drifting;
    q.seed = p.seed;
  }

  void FreeFlight::start(uint32_t nowMs, etl::imessage_bus* bus, double altitudeM) {
    std::lock_guard<std::mutex> lock(mutex_);
    model_.reset(bounded(altitudeM, -500, 10000, 1000));
    active_ = true;
    lastStepMs_ = nowMs;
    steps_ = 0;
    steering_ = speedInput_ = 0;
    injector_.setBus(bus);
    injector_.resetReferenceTime();
    trail_.clear();
    log_.clear();
  }

  void FreeFlight::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
    steering_ = speedInput_ = 0;
  }

  void FreeFlight::controls(double steering, double speedInput, bool straighten) {
    std::lock_guard<std::mutex> lock(mutex_);
    steering_ = bounded(steering, -1, 1, 0);
    speedInput_ = bounded(speedInput, -1, 1, 0);
    controlAtUs_ = Clock::hostUs();
    if (straighten) model_.turnRateDps = 0;
  }

  void FreeFlight::update(uint32_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) return;
    // Release held controls on browser disconnect or lost focus; existing turn trim persists.
    if (Clock::hostUs() - controlAtUs_ > 750000) steering_ = speedInput_ = 0;
    while (static_cast<uint32_t>(nowMs - lastStepMs_) >= 50) {
      const double oldClimb = model_.climbMps;
      model_.step(.05, steering_, speedInput_);
      lastStepMs_ += 50;
      const double h = model_.headingDeg * RAD;
      const double wind = (model_.parameters.windFromDeg + 180) * RAD;
      FlightSensorState sensors;
      sensors.atMs = lastStepMs_;
      sensors.latitude = 47.51 + model_.y / 111320;
      sensors.longitude = 11.18 + model_.x / (111320 * std::cos(47.51 * RAD));
      sensors.altitudeM = model_.z;
      sensors.eastMps =
          std::sin(h) * model_.airspeedMps + std::sin(wind) * model_.parameters.windMps;
      sensors.northMps =
          std::cos(h) * model_.airspeedMps + std::cos(wind) * model_.parameters.windMps;
      sensors.verticalAccelG = (model_.climbMps - oldClimb) / (.05 * 9.80665);
      const auto lines = flightSensorLines(sensors, steps_ % 20 == 0, steps_ % 200 == 0);
      for (const auto& line : lines) {
        injector_.handleLine(line.c_str(), line.size());
        log_.push_back(line);
      }
      if (steps_++ % 10 == 0) {
        trail_.push_back({model_.x, model_.y, model_.z, model_.climbMps, model_.elapsedS});
        if (trail_.size() > 600) trail_.pop_front();
      }
      while (log_.size() > 60000) log_.pop_front();
    }
  }

  FreeFlightSnapshot FreeFlight::snapshot(bool includeTrail) const {
    std::lock_guard<std::mutex> lock(mutex_);
    FreeFlightSnapshot s;
    s.active = active_;
    s.model = model_;
    model_.profile(model_.z, s.centerX, s.centerY, s.radiusM, s.strengthMps);
    if (active_ && includeTrail) s.trail.assign(trail_.begin(), trail_.end());
    return s;
  }

  std::string FreeFlight::recording() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    for (const auto& line : log_) out << line << '\n';
    return out.str();
  }

  FreeFlight& freeFlight() {
    static FreeFlight instance;
    return instance;
  }
}  // namespace sim
