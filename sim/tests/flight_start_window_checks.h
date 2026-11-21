#pragma once

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "logging/flight_auto_start.h"

namespace flight_start_test {
  using Detector = FlightAutoStart;
  using Sample = Detector::Sample;
  constexpr float MPH = .44704f;

  inline Sample motion(uint32_t elapsed, float mph = 0, float vertical = 0) {
    Sample s;
    s.speedMps = mph * MPH;
    s.climbMps = vertical;
    s.altitudeM = 1000 + vertical * (elapsed / 1000.0f);
    s.latitude = 37 + s.speedMps * (elapsed / 1000.0) / 111195.0;
    s.longitude = -122;
    s.speedValid = s.climbValid = s.altitudeValid = s.positionValid = true;
    return s;
  }

  inline void checkFlightStartWindows() {
    // Pure forward motion: full window then one second of confirmation.
    for (uint32_t period : {250u, 1000u}) {
      Detector d;
      for (uint32_t t = 0; t <= 4000; t += period) {
        const bool start = d.update(t, motion(t, 5));
        assert(start == (t == 4000));
      }
      assert(d.triggered() & Detector::Forward);
    }

    // A 4 mph reading among 6 mph readings does not erase the speed window.
    Detector dip;
    for (uint32_t t = 0; t <= 4000; t += 1000) {
      Sample s = motion(t, 6);
      if (t == 2000) s.speedMps = 4 * MPH;
      assert(dip.update(t, s) == (t == 4000));
    }
    assert(dip.window().meanSpeedMps > 5 * MPH);

    // Neither high reported speed without travel nor pressure spikes without
    // agreeing vertical velocity may create a launch.
    for (unsigned mode = 0; mode < 3; ++mode) {
      Detector d;
      for (uint32_t t = 0; t < 60000; t += 1000) {
        Sample s = motion(0);
        s.speedMps = mode == 0 ? 20 : (t % 5000 == 1000 ? 1000 : 0);
        if (mode == 2 && t % 5000 == 1000) s.altitudeM += 5;
        assert(!d.update(t, s));
      }
    }

    for (float sign : {-1.f, 1.f}) {
      // GPS-free ascent/descent needs actual pressure departure and signed vario,
      // then one further elapsed second of qualification in the same direction.
      for (uint32_t period : {250u, 1000u}) {
        Detector vertical;
        for (uint32_t t = 0; t <= 4000; t += period) {
          Sample s = motion(t, 0, sign);
          s.speedValid = s.positionValid = false;
          assert(vertical.update(t, s) == (t == 4000));
          if (t == 3000) assert(vertical.candidates() == Detector::Vertical);
        }
        assert(vertical.triggered() == Detector::Vertical);
      }

      // A pressure excursion that only qualifies once cannot start a flight.
      // After the failed window, a later excursion needs its own confirmation.
      Detector excursion;
      for (uint32_t t = 0; t <= 8000; t += 1000) {
        Sample s = motion(t, 0, sign);
        if (t >= 4000) s.altitudeM -= 3 * sign;
        assert(excursion.update(t, s) == (t == 8000));
      }

      // An ascent candidate and a descent candidate cannot share confirmation.
      Detector reversal;
      for (uint32_t t = 0; t <= 3000; t += 1000) assert(!reversal.update(t, motion(t, 0, sign)));
      Sample reverse = motion(4000, 0, sign);
      reverse.altitudeM = 1000 - 4 * sign;
      reverse.climbMps = -10 * sign;
      assert(!reversal.update(4000, reverse));
      assert(reversal.candidates() == Detector::Vertical);
      reverse.altitudeM = 1000 - 5 * sign;
      reverse.climbMps = -sign;
      assert(reversal.update(5000, reverse));
      assert(reversal.triggered() == Detector::Vertical);

      // Confirmation measures elapsed time, even with repeated checks.
      Detector elapsed;
      for (uint32_t t = 0; t <= 3000; t += 1000) assert(!elapsed.update(t, motion(t, 0, 2 * sign)));
      for (unsigned i = 0; i < 10; ++i) assert(!elapsed.update(3000, motion(3000, 0, 2 * sign)));
      assert(!elapsed.update(3999, motion(3999, 0, 2 * sign)));
      assert(elapsed.update(4000, motion(4000, 0, 2 * sign)));
      elapsed.reset();
      assert(!elapsed.update(5000, motion(5000, 0, 2 * sign)));

      // The vertical vario comparison is strictly >0.5, in either direction.
      for (float rate : {.5f, .5001f}) {
        Detector d;
        for (uint32_t t = 0; t <= 4000; t += 1000) {
          Sample s = motion(t, 0, sign);
          s.climbMps = sign * rate;
          d.update(t, s);
        }
        assert(bool(d.triggered() & Detector::Vertical) == (rate > .5f));
      }

      // The proposed 75%-speed / 50%-vertical mixed launch hits the boundary.
      Detector blended;
      for (uint32_t t = 0; t <= 4000; t += 1000) {
        Sample s = motion(t, 3.75f, .5f * sign);
        s.climbMps = .25f * sign;
        assert(blended.update(t, s) == (t == 4000));
      }
      assert(blended.triggered() == Detector::Blended);

      // Mean vario and net pressure departure must agree on direction.
      Detector disagreement;
      for (uint32_t t = 0; t < 12000; t += 1000) {
        Sample s = motion(t, 3.75f, sign);
        s.climbMps = -sign;
        assert(!disagreement.update(t, s));
      }

      // Thirty minutes of ground-like motion cannot creep across a fading score.
      Detector hiking;
      for (uint32_t t = 0; t <= 1800000; t += 1000)
        assert(!hiking.update(t, motion(t, 3.35f, .3f * sign)));
      Detector closestRecorded;
      for (uint32_t t = 0; t < 30000; t += 1000) {
        Sample s = motion(t, 3.97f, sign / 3);
        s.climbMps = sign * .417f;
        assert(!closestRecorded.update(t, s));
      }
    }

    Detector oscillation;
    for (uint32_t t = 0; t < 30000; t += 1000) {
      Sample s = motion(t, 0, 1);
      s.climbMps = t % 2000 ? -1 : 1;
      assert(!oscillation.update(t, s));  // abs(mean), not mean(abs).
    }

    // Altitude and GPS validity are independent: fresh speed still works without
    // a barometer; fresh pressure/climb still works without GPS.
    Detector noBaro;
    for (uint32_t t = 0; t <= 4000; t += 1000) {
      Sample s = motion(t, 6);
      s.altitudeValid = s.climbValid = false;
      assert(noBaro.update(t, s) == (t == 4000));
    }
    Detector lostAltitude;
    for (uint32_t t = 0; t <= 6000; t += 1000) {
      Sample s = motion(t, 0, 1);
      if (t == 1000) s.altitudeValid = false;
      assert(lostAltitude.update(t, s) == (t == 6000));
    }

    // Invalid speed must invalidate its whole window and restart confirmation.
    Detector lostSpeed;
    for (uint32_t t = 0; t <= 9000; t += 1000) {
      Sample s = motion(t, 6);
      if (t == 4000) s.speedValid = false;
      const bool result = lostSpeed.update(t, s);
      assert(result == (t == 9000));
    }

    Detector gap;
    for (uint32_t t : {0u, 1000u, 2000u, 3000u, 6000u, 7000u, 8000u, 9000u, 10000u})
      assert(gap.update(t, motion(t, 6)) == (t == 10000));
    Detector repeated;
    for (uint32_t t = 0; t <= 3000; t += 1000) assert(!repeated.update(t, motion(t, 6)));
    for (unsigned i = 0; i < 100; ++i) assert(!repeated.update(3000, motion(3000, 6)));
    assert(repeated.update(4000, motion(4000, 6)));
    repeated.reset();
    assert(!repeated.update(4000, motion(4000, 6)));

    Detector irregular;
    for (uint32_t t : {0u, 2000u, 4000u}) irregular.update(t, motion(t, 0, 1));
    assert(irregular.candidates() == Detector::Vertical);
    assert(irregular.triggered() == Detector::None);
    assert(std::abs(irregular.window().altitudeChangeM - 3) < .001f);
    assert(std::abs(irregular.window().meanClimbMps - 1) < .001f);
    assert(irregular.update(5000, motion(5000, 0, 1)));
    assert(irregular.triggered() == Detector::Vertical);
    Detector rollover;
    for (uint32_t t = 0; t <= 4000; t += 1000)
      assert(rollover.update(UINT32_MAX - 1500 + t, motion(t, 6)) == (t == 4000));

    Detector invalid;
    for (uint32_t t = 0; t < 30000; t += 1000) {
      Sample s = motion(t, 20, 10);
      s.speedMps = NAN;
      s.altitudeM = INFINITY;
      s.longitude = NAN;
      assert(!invalid.update(t, s));
    }

    // Longitude interpolation and distance must take the short route at the dateline.
    Detector dateline;
    for (uint32_t t = 0; t <= 4000; t += 1000) {
      Sample s = motion(t, 6);
      s.latitude = 0;
      s.longitude = 179.99999 + s.speedMps * (t / 1000.0) / 111195;
      if (s.longitude > 180) s.longitude -= 360;
      assert(dateline.update(t, s) == (t == 4000));
    }
    assert(dateline.window().displacementM > 8 && dateline.window().displacementM < 9);

    // Even finite endpoint values can overflow a derived float difference; that
    // must invalidate altitude evidence instead of manufacturing a vertical start.
    Detector overflow;
    for (uint32_t t = 0; t <= 5000; t += 1000) {
      Sample s = motion(0);
      s.speedMps = s.climbMps = std::numeric_limits<float>::max();
      s.altitudeM =
          t < 2000 ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
      assert(!overflow.update(t, s));
      assert(std::isfinite(overflow.score()));
    }

    // Opposite vertical directions cannot share the one-second mixed confirmation.
    Detector switched;
    for (uint32_t t = 0; t <= 3000; t += 1000) {
      Sample s = motion(t, 3.75f, .5f);
      s.climbMps = .25f;
      assert(!switched.update(t, s));
    }
    Sample reverse = motion(4000, 3.75f);
    reverse.altitudeM = 999;
    reverse.climbMps = -4;
    assert(!switched.update(4000, reverse));
    assert(switched.candidates() & Detector::Blended);
    reverse = motion(5000, 3.75f);
    reverse.altitudeM = 999.5f;
    reverse.climbMps = -.25f;
    assert(switched.update(5000, reverse));
    assert(switched.triggered() == Detector::Blended);

    // Exact sign reflection must preserve all decisions, including confirmation.
    Detector original, mirrored;
    float altitude = 1000;
    for (uint32_t t = 0; t < 600000; t += 1000) {
      Sample s = motion(t, 4 + std::sin(t / 8000.0));
      s.climbMps = std::sin(t / 5000.0);
      altitude += s.climbMps;
      s.altitudeM = altitude;
      if (t % 23000 == 0) s.altitudeValid = false;
      Sample other = s;
      other.climbMps = -s.climbMps;
      other.altitudeM = -s.altitudeM;
      assert(original.update(t, s) == mirrored.update(t, other));
      assert(original.candidates() == mirrored.candidates());
      assert(original.triggered() == mirrored.triggered());
    }

    puts("Flight-start windows, boundaries, dropouts, and sign symmetry passed");
  }
}  // namespace flight_start_test
