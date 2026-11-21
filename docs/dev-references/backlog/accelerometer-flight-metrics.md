---
title: Accelerometer Flight Metrics Validation
description: Remaining flight validation for sustained g-load metrics and diagnostics.
---

# Accelerometer Flight Metrics Validation

Status reconciled **2026-10-28**: sustained g-load filtering and interval diagnostics merged in
`c7697fe8` (#431). The implementation filters both supported IMU paths and shows fresh filtered
g-load on the User page. Logbook extrema use flight-eligible history and represent unavailable
results as null.
V3 IGC diagnostics preserve interval peaks and pending, committed, and discarded extrema;
see [Flights and Logging](../flights_and_logging.md#g-load-diagnostic-igc-records).

The candidate shares the timer's corroborated motion-window launch evidence, including its
confirmed forward/vertical/blended paths. Automatic starts reuse the accepted check; manual
starts and relaunches collect fresh evidence independently of the Auto Start setting. It keeps
twenty quiet ground checks, the 3 g/s slew limit, ten-sample average, 15-second launch exclusion
and 30-second buckets. Landing still has separate activity/validity checks, so losing launch
qualification during slow flight does not itself indicate touchdown.
These are experimental thresholds rather than completed flight validation.

October 25/27 field evidence and eligibility replay:

- The pilot confirmed an October 27 spiral descent with Redstrap, Blue and v3.2.8 carried together
  relatively securely. The two IMU families captured sustained interval peaks of 3.468, 3.443 and
  3.464 g. Preserve slew/averaging while addressing phase eligibility.
- Replayed 26,947 recorded checks from 29 files through the candidate C++ launch/tracker code.
  Bowen's October 25 first commit moves from recorded 13:49:57 (ground) to approximately 14:04:27
  (after launch). One accepted short prelaunch episode remains and is discarded before committing.
- October 27 Blue 01 is confirmed entirely ground-only. Its actual automatic start still qualifies,
  but the candidate no longer rearms on the later uncorroborated handling episode; no extrema
  commit in the replay (the original file saved 0.918–1.497 g). This does not establish general
  immunity to sustained ground activity that satisfies the shared launch test.
- All ten flight-containing recordings reach eligibility/commit; the October 27 spiral lies well
  within their eligible history. No in-flight landing transition was found against the accepted
  launch intervals and landing references. Raccoon's late October 27 ground confirmation remains.
- Replay is an eligibility check using recorded speed, VAR, pressure altitude and GPS position,
  with a synthetic 20 Hz 1 g stream for guard/bucket timing. It does not replay the original filter.
  V2 START histories support the actual auto-start handoff. Older V1 files lack pre-start altitude/
  position and are modeled as manually opened logs at their recorded start. Post-start altitude
  and VAR rounding and approximated validity limit subsecond/threshold precision.
- October 27 recorded zero explicit filter resets after the continuity fixes, with three ground
  sample gaps. V3 now distinguishes explicit reset causes and automatic sample-gap averaging
  restarts, and exports the largest accepted-sample gap and its ending timestamp per interval.

Remaining work:

- Fly this shared-evidence candidate, logging before launch through landing/packing and noting event
  times. #431 did not include a device flash; the later launch-detection PR #432 records a
  both-slot flash with hash verification of its integrated candidate. That establishes
  installation, not exact-release-candidate qualification. The October 27 data validates both
  IMU families' sustained-load path but predates this eligibility/diagnostic integration.
- Validate sustained hovering and low-groundspeed flight, plus top landing and relaunch.
- Check whether confirmed airborne entry starts the launch exclusion at a useful point.
- Compare discarded history with real maneuvers near landing before changing bucket lengths.
- Decide the preferred tradeoff when ground and hovering remain indistinguishable. Missing
  detector inputs preserve the current airborne state; twenty valid quiet checks still land.
- Use full-rate motion recordings if changing slew/averaging behavior; interval extrema cannot
  reconstruct the original sample sequence.
- Assess the additional IGC diagnostic SD traffic, instrument timing and target memory in the
  exact candidate's runtime reports. Tune/test the shared launch evidence against slow launches
  and sustained ground motion; g-load landing and timer Auto Stop still serve different purposes.
