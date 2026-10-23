---
title: Accelerometer Flight Metrics Validation
description: Remaining flight validation for sustained g-load metrics and diagnostics.
---

# Accelerometer Flight Metrics Validation

The implementation filters both supported IMU paths and shows fresh filtered g-load on the
User page. Logbook extrema use flight-eligible history and represent unavailable results as null.
V2 IGC diagnostics preserve interval peaks and pending, committed, and discarded extrema;
see [Flights and Logging](../flights_and_logging.md#g-load-diagnostic-igc-records).

The candidate uses three airborne checks and twenty ground checks, preserving the original
3 g/s slew limit, ten-sample average, 15-second launch exclusion and 30-second buckets.
These are experimental thresholds rather than completed flight validation.

Remaining work:

- Fly the integrated candidate, logging before launch through landing/packing and noting event
  times. Hardware has not been flashed as part of this implementation.
- Validate sustained hovering and low-groundspeed flight, plus top landing and relaunch.
- Check whether confirmed airborne entry starts the launch exclusion at a useful point.
- Compare discarded history with real maneuvers near landing before changing bucket lengths.
- Decide the preferred tradeoff when ground and hovering remain indistinguishable. Missing
  detector inputs preserve the current airborne state; twenty valid quiet checks still land.
- Use full-rate motion recordings if changing slew/averaging behavior; interval extrema cannot
  reconstruct the original sample sequence.
