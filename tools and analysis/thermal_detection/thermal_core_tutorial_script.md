# Thermal Core Simulator Tutorial Script

This document is the source-of-truth draft for tutorial wording and teaching flow in `thermal_core_game.html`. Edit this script first when adding or revising tutorial scenarios, then update the simulator implementation to match.

## Script Fields

Each tutorial stage should define:

- **Stage title**: Short label for the tutorial card.
- **Initial heading**: Heading shown before the simulator starts the stage.
- **Initial explanation**: Teaching text shown before the user presses Space.
- **Initial goal**: The first measurable action, usually an entry or turn-start task.
- **Flying pop-up text**: Temporary map text shown while the user is flying.
- **Pause heading**: Heading shown when the simulator pauses for the next explanation.
- **Pause explanation**: Teaching text shown before the user continues.
- **Practice heading**: Heading shown during the main practice portion.
- **Practice guidance text**: Dynamic or static text shown while the user practices.
- **Practice goal**: Success criteria for completing the stage.
- **Map view**: Thermal visual, rings, target turn-rate marks, and other aids shown or hidden.
- **Completion text**: Text shown after the stage succeeds.

## Shared Entry Sequence

### Initial Heading

Start of stage

### Initial Explanation

Fly straight into the lift. When the vario peaks and just begins to fade, you are about halfway through the core. That is the moment to start turning.

### Initial Goal

Wait for the vario peak, then turn about 90 degrees. Press Space bar to start.

### Flying Pop-Up Text

START TURNING NOW

Turn about 90 degrees

### Trigger

Show the pop-up when the vario has exceeded 1.0 m/s, the run has lasted at least 6 seconds, and the current vario has dropped at least 0.25 m/s below the peak seen so far.

### Pause Trigger

Pause after the user's heading changes by about 90 degrees from the heading at the pop-up.

## Stage 1: Center With Visual Help

### Initial Heading

1. Center with visual help

### Initial Explanation

Fly straight into the lift. When the vario peaks and just begins to fade, you are about halfway through the core. That is the moment to start turning.

### Initial Goal

First goal: wait for the peak, then turn about 90 degrees. Press Space bar to start.

### Pause Heading

Now center the thermal

### Pause Explanation

Now the goal is to keep the vario constant. If it starts to drop, you are moving away from the center and need to tighten your turn. If it starts to increase, you are moving toward the center and need to widen your turn. Try to fly a few orbits centered on the thermal.

### Pause Goal

Press Space bar or Continue to begin the orbit practice.

### Practice Heading

1b. Center by sound

### Practice Guidance Text

Hold this turn and listen. If the beeps fade, tighten slightly. If they build, widen slightly.

Dynamic alternates:

- Build a smooth turn with the arrow keys.
- Lift is improving: widen slightly so your circle moves toward the core.
- Lift is fading: tighten slightly to avoid sliding away from the core.
- That orbit wandered too much. Re-center and try for two smoother laps.

### Practice Goal

Complete 2 consecutive smooth orbits at any radius, with radial spread under 25% of the thermal diameter.

### Map View

- Thermal visual: shown as fuzzy green blob.
- Outer thermal boundary: shown.
- Concentric rings: shown.
- Ideal turn-rate marks: hidden.
- Thermal Core widget: shown.
- Turn/bank inset: shown.

### Completion Text

Good. You used the vario trend and visual rings to move your circle toward the thermal center.

## Stage 2: Core At A Target Rate

### Initial Heading

2. Core at a target rate

### Initial Explanation

This time the green target bands are back. They represent the nominal turn rate that gives an efficient target radius around the core.

### Initial Goal

First goal: wait for the peak, then turn about 90 degrees. Press Space bar to start.

### Pause Heading

Now core the thermal

### Pause Explanation

Now add the target turn-rate habit. Use vario changes to slide your circle onto the center, then return toward the green target band so the bank stays efficient.

### Pause Goal

Press Space bar or Continue to begin the orbit practice.

### Practice Heading

2b. Core at target rate

### Practice Guidance Text

Use the green target bands as your home base. Tighten or widen just enough to move the circle, then settle back near the target.

Dynamic alternates:

- Build a smooth turn with the arrow keys.
- Lift is improving: widen slightly so your circle moves toward the core.
- Lift is fading: tighten slightly to avoid sliding away from the core.
- That orbit missed the target radius. Re-center, return to the green band, and try again.

### Practice Goal

Complete 2 consecutive target-radius orbits where maximum radial error is no more than +/-10% of the thermal diameter from the specific target radius implied by the target turn rate.

Target radius is:

```text
target radius = airspeed / target turn rate in radians per second
```

### Map View

- Thermal visual: shown as fuzzy green blob.
- Outer thermal boundary: shown.
- Concentric rings: shown.
- Ideal turn-rate marks: shown.
- Thermal Core widget: shown.
- Turn/bank inset: shown.

### Completion Text

Good. You held a radius close to the target turn-rate circle.

## Stage 3: Center Without Rings

### Initial Heading

3. Center without rings

### Initial Explanation

Try the same entry and centering exercise again, but without the concentric rings. You still get the fuzzy green thermal shape, but the constant-vario path is no longer drawn for you.

### Initial Goal

First goal: wait for the peak, then turn about 90 degrees. Press Space bar to start.

### Pause Heading

Now center without rings

### Pause Explanation

Same idea: keep the vario as constant as possible. Fading beeps mean tighten; building beeps mean widen.

### Pause Goal

Press Space bar or Continue to begin the orbit practice.

### Practice Heading

3b. Fuzzy-blob centering

### Practice Guidance Text

Use the sound first. Let the fuzzy thermal visual confirm what your ears are telling you.

Dynamic alternates:

- Build a smooth turn with the arrow keys.
- Lift is improving: widen slightly so your circle moves toward the core.
- Lift is fading: tighten slightly to avoid sliding away from the core.
- That orbit wandered too much. Re-center and try for two smoother laps.

### Practice Goal

Complete 2 consecutive smooth orbits at any radius, with radial spread under 25% of the thermal diameter.

### Map View

- Thermal visual: shown as fuzzy green blob.
- Outer thermal boundary: hidden.
- Concentric rings: hidden.
- Ideal turn-rate marks: shown.
- Thermal Core widget: shown.
- Turn/bank inset: shown.

### Completion Text

Good. You centered the thermal without the constant-radius guide rings.

## Stage 4: Center By Vario Only

### Initial Heading

4. Center by vario only

### Initial Explanation

Final basic drill: the thermal visual is hidden. Fly the same technique using only the vario tone and the flight instruments.

### Initial Goal

First goal: wait for the peak, then turn about 90 degrees. Press Space bar to start.

### Pause Heading

Now fly by sound only

### Pause Explanation

Trust the vario. If it fades, tighten. If it builds, widen. The goal is still a steady-radius circle centered on the lift.

### Pause Goal

Press Space bar or Continue to begin the orbit practice.

### Practice Heading

4b. Sound-only centering

### Practice Guidance Text

Listen for symmetry. Try to make each orbit sound boring and steady.

Dynamic alternates:

- Build a smooth turn with the arrow keys.
- Lift is improving: widen slightly so your circle moves toward the core.
- Lift is fading: tighten slightly to avoid sliding away from the core.
- That orbit wandered too much. Re-center and try for two smoother laps.

### Practice Goal

Complete 2 consecutive smooth orbits at any radius, with radial spread under 25% of the thermal diameter.

### Map View

- Thermal visual: hidden.
- Outer thermal boundary: hidden.
- Concentric rings: hidden.
- Ideal turn-rate marks: shown.
- Thermal Core widget: shown.
- Turn/bank inset: shown.

### Completion Text

Congratulations, you can center a thermal with only the vario.

## Post-Tutorial Options

After the basic tutorial, offer the user follow-up drills:

- Try another random invisible thermal.
- Repeat the same invisible thermal to compare attempts.
- Enter pointed near the thermal center.
- Enter offset toward the inner edge.
- Enter offset toward the outer edge.
- Cross through the middle from different approach angles.
- Add wind, diameter variance, strength variance, and lumpiness in controlled steps.

## Future Scenario Template

### Stage Title

TBD

### Initial Heading

TBD

### Initial Explanation

TBD

### Initial Goal

TBD

### Flying Pop-Up Text

TBD

### Pause Heading

TBD

### Pause Explanation

TBD

### Pause Goal

TBD

### Practice Heading

TBD

### Practice Guidance Text

TBD

### Practice Goal

TBD

### Map View

- Thermal visual: TBD.
- Outer thermal boundary: TBD.
- Concentric rings: TBD.
- Ideal turn-rate marks: TBD.
- Thermal Core widget: TBD.
- Turn/bank inset: TBD.

### Completion Text

TBD
