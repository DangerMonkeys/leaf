# Route guidance lab

This simulator-only lab uses synthetic routes to compare inward margins and
inspect the Navigate LCD. The map shows the real cylinder separately from the
inward aim. The lab replaces the firmware's guidance while active. For the
production behavior and timing, see [Firmware route guidance](FIRMWARE.md).

## Live fly the LCD and map

Build and run `leafsim` using the normal sim README instructions. Open its control
page, find **Route guidance lab**, choose a route and policy, and click
**Start / restart route trial**. This creates an isolated in-memory route, resets
the pilot west of TP1 at 1,500 m, enables flight logging and the Navigate page,
and waits for a fresh relocated GPS fix. The preset replaces simulator navigation
state; use a separate simulator state/card directory for this experiment.

- Begin with **Six grazing cylinders / Grazing only · up to 30 m**.
- Use left/right arrows to change turn rate, **C** to straighten, up/down to change
  airspeed, and **Space** to pause. Mouse flight buttons are also available.
- Fly the LCD arrow. Solid blue is the active valid cylinder, green is a future
  cylinder, gray is credited, dashed orange is the inward planning boundary, and
  orange dots/line are the actual targets used for guidance. Purple is GPS.
- Keep **Fit route on map** enabled for an overview. Disable it and adjust map
  zoom for local entry inspection. The map has +/− buttons and a zoom slider. Hide the thermal overlay if it obscures a point.
  At desktop widths the LCD stays visible when scrolling. Its zoom-out control
  makes a 3x LCD fit beside the map on shorter screens.
- The cost readout shows two complete planned distances from the original preset
  start: the selected margin and no margin, plus percentage extra. Both use the
  same 512-iteration nominal solution and stop at the actual planar goal boundary.
  They stay fixed as you fly; changing policy recalculates the selected cost. The
  live remaining-distance readout continues to follow your GPS position.
- Change the margin policy during flight without losing the warm nominal solution.
  Pause/step to inspect the next update. Restart to compare the same route.
- The **Mixed angles** seven-point route combines grazing passes at TP1/TP2,
  a straight-in out-and-back reversal at TP3, another grazing pass at TP4,
  and approximately 45/60 degree intercepts at TP5/TP6 before goal. Angles
  describe the ideal unbuffered path from the preset start; they change as you
  deviate and as margins adjust targets. Follow the waypoint numbers: the
  east/west return leg deliberately retraces part of the outbound leg.
- Try right-angle turns, 35 m cylinders (center fallback), and overlaps next.
  Overlapping cylinders may sequence without reaching the displayed target;
  real-cylinder entry remains authoritative.
- Save **guidance trace** and **flight recording** after an interesting run.
  The trace retains the latest 3,600 processed fixes, including policy, zero-based
  active point, measured position, target, bearing, turn, distance, and margin.
  The recording contains synthetic sensor messages and can replay flight motion;
  it does not itself restore the lab's route/policy setup.

Useful review observations: arrow hunting near contact, how early the arrow changes
after real cylinder entry, whether the inward target feels excessive on a direct
approach, and whether small/overlapping cylinders behave understandably. The live
free-flight physics differs from the automated benchmark pilot and has no added
GPS noise; it primarily assesses usability and actual LCD integration.

**Turn experiment off** releases the simulator-only override. The firmware
can then plan the active route using its normal optimizer. Starting an ordinary free flight or recorded scenario also
disables the experiment. Cancelling/changing the route cannot reuse stale targets.
The lab is off after a process restart.

## Verification

`make -C sim test-route-guidance` checks the displayed bearing, cylinder
distance, actual-cylinder sequencing, live policy changes, goal handling,
trace export, and LCD pointer geometry. `make -C sim test-route-solver`
checks the standalone geometry and margin policies.
