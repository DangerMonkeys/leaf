#include <Arduino.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>

#include "instruments/baro.h"
#include "instruments/imu.h"
#include "runtime.h"
#include "sim/clock.h"
#include "ui/settings/settings.h"

namespace {
  uint32_t origin;
  bool gravityMode;
  bool varyingClimb = false;
  double altitude() {
    const double seconds = uint32_t(millis() - origin) * .001;
    return 100 + 2 * seconds + (varyingClimb ? 10 * sin(seconds * .3) : 0);
  }
  void pressure() {
    const int32_t p = lround(101325 * pow(1 - altitude() / 44331, 1 / .190264));
    baro.on_receive(PressureUpdate(millis(), p));
  }
  MotionUpdate sample(uint32_t t) {
    MotionUpdate m(t);
    m.hasAcceleration = true;
    m.hasGravity = gravityMode;
    m.hasOrientation = !gravityMode;
    m.az = gravityMode ? .99 : 1;
    m.gz = 1;
    return m;
  }
  void motion() { imu.on_receive(sample(millis())); }
  void advance(uint32_t ms) { sim::clock().advanceUs(uint64_t(ms) * 1000); }
  void step(uint32_t ms = 50) {
    advance(ms);
    pressure();
    motion();
  }
  void ready() {
    assert(imu.velocityValid());
    assert(imu.startupSamplesCompleted() == imu.startupSamplesRequired());
    if (std::abs(imu.kalmanVelocity() - 2) >= .15)
      fprintf(stderr, "Continuity mismatch: gravity=%d t=%u velocity=%g bias=%g\n", gravityMode,
              uint32_t(millis()), imu.kalmanVelocity(), imu.accelerationBias());
    assert(std::abs(imu.kalmanVelocity() - 2) < .15);
  }
  void warm() {
    baro.sleep();
    baro.wake();
    imu.wake();
    baro.invalidateClimb();
    origin = millis();
    for (int i = 0; i < 1200; ++i) step();
    ready();
  }
  void checkDisplayAverage() {
    warm();
    varyingClimb = true;
    Barometer displayBaro;
    displayBaro.setClimbDisplayAverageSeconds(20);
    std::deque<int32_t> blocks;
    int32_t blockSum = 0;
    unsigned samples = 0;
    bool differsFromFiveSeconds = false;
    for (unsigned i = 0; i < 1300; ++i) {
      step();
      displayBaro.on_receive(PressureUpdate(millis(), baro.pressure()));
      if (!displayBaro.climbRateFilteredValid()) continue;
      blockSum += displayBaro.climbRateFiltered();
      if (++samples % 4) continue;
      blocks.push_back(blockSum / 4);
      blockSum = 0;
      if (blocks.size() > 100) blocks.pop_front();
      int32_t sum = 0, recentSum = 0;
      for (unsigned j = 0; j < blocks.size(); ++j) {
        sum += blocks[j];
        if (j + 25 >= blocks.size()) recentSum += blocks[j];
      }
      const int32_t expected = sum / static_cast<int32_t>(blocks.size());
      assert(displayBaro.climbRateForDisplay() == expected);
      if (blocks.size() == 100 && std::abs(expected - recentSum / 25) > 50)
        differsFromFiveSeconds = true;
    }
    assert(differsFromFiveSeconds);  // Verifies the full twenty-second window, including wrap.
    displayBaro.setClimbDisplayAverageSeconds(0);
    assert(displayBaro.climbRateForDisplay() == displayBaro.climbRateFiltered());
    varyingClimb = false;
  }
}  // namespace

int main() {
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/vario-continuity/state";
  options.cardRoot = "sim/build-tests/vario-continuity/card";
  sim::runtime().configure(options);
  sim::runtime().boot();
  settings.loadDefaults();
  for (bool mode : {false, true}) {
    gravityMode = mode;
    warm();
    for (uint32_t gap : {200, 250, 500, 501, 1000, 1500}) {
      imu.gLoad().takeInterval();
      advance(gap);
      assert(imu.accelValid() == (gap <= 500));
      assert(baro.pressureFresh() == (gap <= 500));
      assert(imu.velocityValid() == (gap <= 500));
      if (gap > 500) {
        baro.invalidateClimb();
        assert(!baro.climbRateFilteredValid());
      }
      pressure();
      motion();
      ready();
      assert(imu.gLoad().takeInterval().resets == 0);
      step();
      // This long-running average would need another 20 samples if stale output cleared it.
      assert(baro.climbRateAverageValid());
    }
    // Neither a stale message nor a temporary pressure-only outage resets the filter.
    const auto updates = imu.kalmanUpdateSampleCount();
    auto stale = sample(uint32_t(millis() - 501));
    stale.hasGravity = !mode;  // A stale mode mismatch must also be discarded harmlessly.
    imu.on_receive(stale);
    assert(imu.kalmanUpdateSampleCount() == updates);
    ready();
    for (int i = 0; i < 16; ++i) {
      advance(50);
      motion();
    }
    assert(!imu.velocityValid() && imu.kalmanValid());
    const double heldBias = imu.accelerationBias();
    assert(!mode || std::abs(heldBias + .0980665) < .01);
    pressure();
    advance(1);
    motion();
    ready();
    step();
    assert(baro.climbRateAverageValid());
    // An explicit sensor reset still requires the original startup qualification.
    MotionUpdate reset(millis());
    reset.reset = true;
    imu.on_receive(reset);
    baro.invalidateClimb();
    assert(!imu.kalmanValid() && !imu.velocityValid());
    for (int i = 0; i < 19; ++i) {
      step();
      assert(!imu.velocityValid());
    }
    step();
    assert(imu.velocityValid());
    // The 1500 ms core guard is exercised independently of the 500 ms freshness check.
    step(1501);
    assert(imu.kalmanVelocity() == 0);
  }
  checkDisplayAverage();
  // Freshness and motion gap subtraction must also survive the 32-bit millisecond rollover.
  gravityMode = true;
  const uint32_t nearWrap = UINT32_MAX - 1000;
  sim::clock().clearTimers();
  advance(nearWrap - uint32_t(millis()));
  warm();
  step(500);
  ready();
  puts(
      "Both IMU paths: delay continuity, 500 ms freshness, pressure outage, genuine reset and "
      "rollover passed");
}
