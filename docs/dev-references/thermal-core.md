---
title: Thermal Core
description: How the live thermal centring estimate and its display page work, and why they are built this way.
---

# Thermal Core

Thermal Core is a Leaf Labs page that answers two questions while the pilot is circling: **is this
thermal worth staying in**, and **where should the circle be**. It is live guidance about the climb
happening right now, and is unrelated to `ThermalTracker`, which remembers thermals already flown
so they can be found again later.

```
src/vario/navigation/thermal_core_solver.{h,cpp}   the maths, no hardware dependencies
src/vario/navigation/thermal_core.{h,cpp}          sensors, state, and the drawing frame
src/vario/ui/display/pages/primary/page_thermal_core.cpp
src/vario/tests/thermal_core_tests.cpp             the solver against synthetic flights
sim/recordings/thermal-core.json                   the reference flight everything is tuned on
```

## The three things that make it work

Almost all of the difficulty is in the three corrections below. An estimator that skips them
produces a marker that points confidently in the wrong direction, which is worse than no marker.

**A thermal is stationary in the air mass, not over the ground.** In the reference flight the wind
is 2 m/s and a lap takes 18 s, so the air — and the thermal in it — moves 36 m per lap against a
27 m circle radius. Over the ground the flown path is a stretched spiral that no circle fit
survives. Every observation is therefore carried forward to where its parcel of air has drifted to
by now, using `windEstimator`, before anything is fitted to it. `WindEstimate` is treated as
optional: without it the fit falls back to the ground frame, which is no worse than not correcting.

**Climb describes air the glider was in about two seconds ago.** The barometer's fusion plus the
1 s average that feeds these observations put the reading that much behind the aircraft. At 9.5 m/s
that is 19 m — most of a circle radius, or a fifth of a lap of rotation. Each climb value is paired
with the position interpolated at `timeS - CLIMB_LAG_S`, and the trail is drawn at those positions
too, so what is on screen is where the air is rather than where the glider was.

**A thermal is usually entered off a glide, and the straight run in stays in the buffer for the
first half minute of circling.** A 12 s straight leg is a 120 m chord; fitted together with the
circle it doubles the apparent radius and reads the lift profile at the wrong bearings. The fit
walks back from the newest sample and stops where the turning stopped, and also stops after two
laps — older than that is a circle the pilot has already moved on from.

## The estimate

With the observations in the air frame and the lag taken out:

1. **Circle fit.** A weighted Kåsa fit (algebraic least squares, closed form, no iteration) gives
   the centre and radius of the circle being flown, plus how round the path actually was.
2. **Harmonic fit.** Lift is fitted around the lap as `a0 + a1·cos θ + b1·sin θ`, where θ is the
   bearing from the fitted circle centre. This first harmonic is the whole answer: its **phase** is
   the direction of the core from the circle centre, and its **size relative to the mean** is how
   badly the circle is placed.
3. **Offset.** `offset = radius × GAIN × asymmetry`, where `asymmetry = amplitude / 2·mean`.

Step 3 deserves a note, because the obvious alternative does not work. Modelling the thermal as a
Gaussian of strength `W` and width `Rc` and inverting for the offset requires knowing `Rc`, and on
a single circle of constant radius `Rc` and the offset are **exactly degenerate** — the lap only
ever pins down the combination `2·R·d/Rc²`. The second harmonic does not help; it gives the same
combination again. So the offset is instead expressed as a fraction of the circle being flown,
which is both honest about what is observable and how pilots already think about it ("shift half a
radius towards the strong side").

`ASYMMETRY_GAIN` is set from the reference flight played through the real firmware, where a 27 m
circle one radius off the core measures an asymmetry of 0.50 — so 2.0 would read that offset back
exactly. It is deliberately set to 1.7. An offset that under-reads walks the pilot in from one side
over two laps; one that over-reads flies them past the core and hunts, and the same constant has to
serve cores narrower and wider than this one.

**Confidence** is the product of four terms, because any one of them being bad is enough to make
the marker misleading: how much of a lap the segment covers, how round the path was, whether there
is enough lift to be talking about, and whether the harmonic is bigger than the scatter around it.
The last term has a floor, so a lap with no asymmetry *and* no scatter scores high — that is a
centred circle, the best answer available, and must not be scored as an absent one.

## Circling

The page only commits to a turn layout once the turn has been sustained for four seconds and
covered 200°, and only abandons it once the turn has properly stopped for three. Without the
hysteresis, a page driven straight from a buffer of recent samples keeps drawing a turn for a full
window after the wings are level, and flips layout on a single ragged fix.

## The display

The map is 96×96, track up, and drawn in the air mass, so a centred circle draws as a circle. It is
rotated by the **air** heading rather than the ground track: in wind those differ by a drift angle
that swings through a dozen degrees each lap, which would rock the whole picture once a circle.

The glider is pushed 20 px to the *outside* of its own turn, and the scale is chosen so the flown
circle maps to a 32 px radius. Between them the whole circle fits the window, and a 20 m circle and
a 45 m circle look the same — which is what makes "am I centred" readable at a glance regardless of
how tightly the pilot happens to be turning. The scale is smoothed, and defined while gliding too,
so the view never jumps as a turn starts or stops.

| Symbol | Meaning |
|---|---|
| Trail dots | The air the vario has seen, sized by lift |
| Small `+` | The centre of the circle currently being flown |
| Bullseye with spokes | The estimated core |
| `24m` | How far the circle centre is from the core — the size of the correction |
| `OPEN` | Flatten the turn now |
| `CENTRED` | The circle is on the core; keep turning |

While circling, dot size is banded **relative to the lap's own average**, not against an absolute
climb rate. This is the difference between a display that shows the shape of the thermal and one
that saturates at full size everywhere inside a strong one: an even necklace of rings means
centred, a fat side means go that way. Gliding, the bands are absolute against `vario_climbStart`.

`OPEN` fires when the nose lines up with the direction from the circle centre to the core — *not*
when it points at the core itself, which never happens. The core is inside the circle, so it stays
off to the inside of the nose all the way round. Flying straight walks the centre of the circle
along the nose, so that alignment is the moment when a second or two of straight flight moves the
whole circle onto the core.

## Working on it

The solver has no Arduino dependency, so it compiles and runs on a host on its own:

```sh
g++ -std=gnu++17 -Isrc/vario -o tctest \
    src/vario/tests/thermal_core_tests.cpp src/vario/navigation/thermal_core_solver.cpp main.cpp
```

…given a `Serial` stand-in, which is what `-DRUN_EMBEDDED_TESTS` supplies on device. The tests fly
synthetic circles through a Gaussian core in a wind and check the answer against the geometry that
produced it, including that the wind and lag corrections are the things doing the work.

End to end, the reference flight through the real firmware:

```sh
leafsim --port 0 --speed 0 --accept-warning \
        --setting LAB_THERM_CORE=1 --setting SHOW_THERM_CORE=1 \
        --setting SHOW_THERM_TRK=0 --setting SHOW_NAV=0 \
        --scenario sim/recordings/thermal-core.json --play \
        --script sim/scripts/thermal-core.txt
```

Build with `OPT="-O1 -g -DDEBUG_THERMAL_CORE"` and the estimator prints every number it arrived at
twice a second to the serial console, which can be checked against the geometry that recording
documents in its own header.

## Known limits

- The core's **width** is not observable from one circle, so the offset is a calibrated fraction of
  the turn radius rather than a measurement. A glide straight through a thermal does constrain it —
  sink outside, lift inside, along a known chord — and that is the obvious place to look next if
  the offset needs to be right rather than merely conservative.
- Everything is fitted at 1 Hz, the rate the receiver supplies fixes. An 18 s lap gives 18 samples,
  which is enough for a first harmonic but not for much more.
- There is no total-energy compensation, so a pilot who pumps the wing while circling injects lift
  the estimator will believe.
