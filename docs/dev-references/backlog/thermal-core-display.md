---
title: Thermal Core Display Backlog
description: Implemented Thermal Core guidance, display controls, and remaining field validation.
---

# Thermal Core Display Backlog

Status reconciled **2026-10-22**: Thermal Core is implemented. Guidance/layout improvements
merged in `97ff8083` (#415), display controls and automatic switching in `a6e069a6` (#417),
and lifecycle/readiness fixes plus Basic/User naming in `dd70c883` (#424).

## Implemented behavior

- Thermal Core and Thermal Track are selectable under `Settings > Display`, alongside Basic,
  User and Navigate. The thermal pages are no longer Leaf Labs-only prototypes. At least one
  primary page must remain enabled; legacy page settings migrate to the current names.
- ThermalTracker retains up to 40 roughly 1 Hz detector samples. Each contains both the broad
  30-second climb signal and a 1-second climb average for live guidance. Core samples expose
  the faster climb, position, course, altitude, timestamp, speed and wind projection.
- The track-up map uses a turn-dependent aircraft position, recent lift-weighted breadcrumbs,
  a lift-center target circle and geographic north/south/east/west lines clipped to the map.
  It uses a fixed 1.15 m/pixel scale rather than the original proposed automatic zoom.
- Breadcrumbs use a cross for weak lift, progressively larger 5/7/9/11 px rings and a filled
  strongest-lift marker. The glyph scale expands for stronger thermal episodes; it is not
  the original proposal's uniform 5 px circles with varying ring thickness.
- Center estimation uses the latest complete turn with sufficient lift and spatial coverage,
  angular weighting, altitude-projected samples and smoothing. Partial turns, weak lift and
  nearly straight paths cannot establish a new center. It estimates sampled lift rather than
  locating unseen lift.
- A retained center remains available within 100 m, hides after five continuous seconds outside
  that range, and restores on return. Stale GPS hides guidance without erasing the retained
  center. The map also shows a wind arrow and a graphical steering reticle.
- Optional Auto Thermal Core switches into Core when guidance is available and manages return
  to the previous page. Manual page selection and page-disable behavior are covered by the
  display-settings regression.
- Flight start, stop and orderly shutdown clear thermal histories, saved session thermals,
  fitted guidance/targets, page cursors and automatic-switch state. Collection restarts only
  with new usable GPS and valid climb readings. Barometer readiness guards prevent retained
  guidance from reading unavailable altitude after wake.

## Evidence and iteration

[`sim/README.md`](../../../sim/README.md) documents live thermal training, the display,
center behavior and recording/replay controls. Implementation is in `src/vario/navigation/`
and the Core/Track pages in `src/vario/ui/display/pages/primary/`.

Existing regression targets are `test-thermal-core`, `test-thermal-track`,
`test-free-flight`, `test-display-settings` and `test-thermal-lifecycle`.
The lifecycle bug's recorded simulator/build/flash evidence and remaining device checks are in
[Miscellaneous firmware bugs](misc-firmware-bugs.md).

## Remaining work

- Consider showing a bearing pointer to the next waypoint on Thermal Core and Thermal Track
  while navigation is active. This would help the pilot see exactly when to stop circling and
  head toward the next waypoint without first switching to the Nav page.
- Add a data field with user-selectable options in the blank space on Thermal Core, above
  the map and below the climb rate.
- Evaluate center stability, steering usefulness and readability in representative real flight,
  including weak/irregular lift, strong wind, turn reversals and loss/recovery of GPS.
- Tune estimator and layout from that evidence; the old arrow-size, breadcrumb-style and
  first-prototype tasks are resolved by the implementation.
- Complete repeated device shutdown/charging/wake and flight-stop/restart checks before closing
  the lifecycle bug. Successful simulator regressions and flash writes do not close that check.
