---
title: Thermal Core Display Backlog
description: Future display plan for a thermal core mode that helps pilots center their climbing turn.
---

# Thermal Core Display Backlog

Leaf's thermal tracking work currently detects, saves, and maps previously found thermals. A possible
next step is a live "thermal core" mode: while the pilot is actively circling in lift, estimate the
target circle that should keep the glider better centered in the thermal and present that guidance on
the 96 px wide LCD.

This note captures the current display plan so the work can resume from the same design point.

## Display Goals

- Preserve spatial awareness. The page should show the pilot where they are relative to the target
  circle, not only say "turn more" or "turn less".
- Stay readable at a glance on the small monochrome LCD.
- Prefer one primary spatial idea over multiple competing overlays.
- Support left and right thermaling without wasting half the screen.
- Make recent lift history visible without cluttering the page.

## Proposed Layout

- Draw the pilot as a fixed track-up arrow.
- Bias the arrow horizontally based on turn direction:
  - Left turn: place the arrow slightly right of center, leaving more room for the thermal circle on
    the left.
  - Right turn: place the arrow slightly left of center, leaving more room for the thermal circle on
    the right.
  - Unknown or low-confidence turn direction: fall back to a centered arrow.
- Reuse the black/white glyph style from the existing thermal track page's aircraft pointer in
  `src/vario/ui/display/pages/primary/page_thermal_track.cpp`.
- Show only the current best target circle. Do not also draw the pilot's fitted/current flown circle;
  the breadcrumbs already show recent flown path and a second circle is likely to be confusing.
- Draw north-south and east-west cross lines through the target circle center. These lines should give
  the pilot a stronger sense of the circle center and the display's rotation as the track-up view
  changes.
- Show recent breadcrumbs from the detector buffer, roughly the last 20-40 seconds.
- Draw breadcrumbs as small circles with 5 px diameter.
- Encode relative lift strength with ring thickness:
  - 1 px ring: weaker/recent baseline lift.
  - 2 px ring: moderate lift.
  - 3 px ring: strongest recent lift.
- Keep the aircraft marker visually dominant enough to orient the pilot, but not so large that it
  hides the target circle or nearby breadcrumbs.

## Coordinate Behavior

- Use a track-up transform: the aircraft arrow points up and world coordinates rotate around the
  aircraft according to current GPS course/track.
- The aircraft remains in a fixed screen position for the current turn direction instead of being
  centered on the display.
- Scale should prioritize showing the useful portion of the target circle and recent breadcrumbs.
  It is acceptable for the circle to be clipped by screen edges.
- Prefer showing the next local arc of the target circle over fitting the entire circle at all costs.
- Avoid abrupt zoom or center jumps; smooth scale and circle placement enough that the display feels
  stable while circling.

## Data Inputs

The current thermal detector buffer lives in `ThermalTracker`:

- `MAX_DETECTOR_SAMPLES = 40`.
- Samples are approximately 1 Hz, though GPS updates may occasionally be missed.
- Samples already include local meter coordinates, altitude, GPS course, time, and `climb30Cms`.

For thermal core mode, add or expose a faster climb signal per sample:

- Keep a running roughly 1 second average of baro climb rate.
- Store that value with each detector sample, for example as `climb1sCms` or `climbFastCms`.
- Continue using `climb30Cms` for broad thermal detection and saved thermal spine weighting.
- Use the faster climb value for breadcrumb strength and live core/circle estimation.

## Target Circle Estimation Context

The target circle is conceptually distinct from the current flown circle:

- The estimator may fit the pilot's current circle internally to understand turn geometry.
- The display should only show the target circle that the pilot should migrate toward.
- The target circle should be smoothed and confidence gated before display.
- If confidence is low, consider showing breadcrumbs and the aircraft only, or a clearly degraded
  target circle.

Potential estimator inputs:

- Recent local `xM/yM` positions from the detector buffer.
- Recent course deltas to infer left/right turn direction and turn coverage.
- Recent 1 second averaged climb values to identify stronger portions of the circle.
- Optional lag compensation, where climb observed now is associated with aircraft position a few
  seconds earlier.

## Open Questions

- Exact aircraft arrow size and pixel glyph.
- Exact biased aircraft positions for left and right turns on the 96 px wide display.
- Whether the NS/EW target-center lines should be full-screen clipped lines, short crosshairs, or
  just line segments inside/near the target circle.
- Whether breadcrumb ring thickness is enough to distinguish lift strength on the real LCD, or whether
  strongest lift needs a filled center pixel/cross.
- Whether a tiny text cue such as `HOLD`, `IN`, `OUT`, or `WEAK` should be included as a secondary
  cue after the spatial display is working.
- How much smoothing is needed so the target circle does not jump as detector samples enter and leave
  the 40-sample buffer.

## Suggested First Prototype

1. Add the per-sample 1 second averaged climb signal to the thermal detector data path.
2. Build a Leaf Labs-only thermal core display page using simulated or debug target-circle values.
3. Render:
   - biased track-up aircraft glyph,
   - one target circle,
   - NS/EW cross lines through the target center,
   - 20-40 seconds of lift-weighted breadcrumbs.
4. Tune layout with screenshots or a host-side LCD preview tool if available.
5. Connect the display to the live estimator once the page is readable and stable.
