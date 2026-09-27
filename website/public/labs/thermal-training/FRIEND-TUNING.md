# Thermal with friends / Work together and Harder conditions

## Step 3: Harder conditions

Open `/labs/thermal-training/?lesson=harder-conditions`, or finish Step 2 and
continue. On progression, Step 3 starts at the altitude reached in Step 2 and
requires another 500 m. A direct start begins at 1,000 m. Retrying keeps the
step's starting altitude, resets weather and removes the arriving pilot.

Harder conditions use the same geometry, 6 m/s core, no bank sink, hidden thermal,
colored turn guidance, and NPC steering. Overrides are:

| Parameter | Harder setting |
|---|---|
| Thermal wander speed | 0–2 mph over 10 seconds (2x displacement for a shared seed) |
| Strength range | ±50% of initial net core climb (3–9 m/s at core) |
| Strength change limit | 4 percentage points/second |
| Wind | 6 mph, seeded random direction |
| Arrival trigger | 200 m of user gain, once per attempt |
| Clearance in both steps | At least 20 m Euclidean 3D distance from every NPC, including approaching traffic |

Pilots receive full wind advection. The thermal adds wind drift using the Wind
stage's `thermalWindDriftRatio` (6/7 at the current baseline), on top of its own
wandering. Direction is reproducible for a given scenario seed. The NPC's guard
prediction includes both its wind advection and the thermal's total velocity.

The newcomer approaches from a seeded random bearing, initially 65 m outside an
outer holding orbit. That orbit estimates the two existing pilots' turn centers,
then stays about 25 m outside their radial envelope. After at least 3 seconds
holding, it looks for a 50-degree opening on either side and gradually reduces
its target radius (up to 6 m/s). A closing gap or predicted near encounter yields
outward. It uses the same airspeed, finite turn response, wind, and sampled lift;
no position teleport or scripted altitude following is used. Approach altitude
includes a lead-in allowance so it can arrive near the circling pilots' height.
These are visual-training heuristics, not a collision-avoidance guarantee.

Angular separation is no longer a pass/fail limit in either step. Until the
newcomer joins, the ±25-m vertical limit relative to the original friend remains.
Once joined, that switch is permanent for the attempt: only positive user vario
and 20 m separation from either pilot are checked. Failure stops with feedback and
preserves earned meters. The 500 m target does not reset on arrival or joining.
Both NPCs draw in altitude order in the map, and in distance order using a shared
head direction in the side view. The existing altitude badge tracks the original friend.

## Step 2 reference

Open `/labs/thermal-training/?lesson=work-together` and press Space or Start.
This is a visual training prototype, not a flight-dynamics or collision-avoidance model.

Work together alone uses three visual columns: 10% vario, 45% top-down view,
and 45% side view (less small gutters). The top-down player anchor is at 32.5%
of map width. This column controls projection and zoom only: both NPCs draw
across the full map behind the vario and side-view overlays, clipped only at the
outer map boundary. The side view stays vertically centered, uses up to 72% map height,
and shrinks symmetrically only as needed to clear the actual top HUD cards by
10 CSS pixels. The vario is centered in its own column. Other lessons retain
their existing layouts.
The Work together top-down world scale is doubled (100% zoom increase) without
changing icon sizes. Thermal rings, positions, tracks, and forecasts share that
scale. Top-down pilot icons render in ascending altitude order, so a higher pilot
occludes a lower one at crossings. Equal-altitude ties keep the user on top.

## Behavior

Completion requires a continuous 500 m user climb with positive user vario,
at least 20 m 3D separation, and within ±25 m of the friend's altitude.
Boundary values are allowed. The moving thermal center has no effect on the
distance check. Leaving lift or either spacing limit stops the attempt and preserves
earned meters. Proximity feedback is "Don't get too close to another glider."
Above/below feedback and loss-of-lift feedback remain. Failure stays latched until the
player explicitly restarts. This replaces the old three-orbit completion.
The side-view badge reads `Friend: +12 m` when the friend is above, negative
when below, and turns amber beyond the 25 m vertical limit.

Goal tuning: `goalClimbM: 500`, `trafficClearanceM: 20`,
`goalAltitudeToleranceM: 25`. These checks do not change NPC steering.

Work together now hides the thermal shading and all concentric rings. Colored
turn-rate bands and the user's leading-arc glow remain visible via `showTurnRateBands`, independently of
`showTargetMarkers`, so enabling them does not re-enable spiral sink penalties.
The original NPC uses the traced white/black/white/lime UV skin in both views.
The arriving second NPC replaces only the lime fabric with orange-red (#ff4f2a),
including the 3D canopy underside and static fallback; white, black, shading and
pilot colors are preserved. NPC turn forecasts are hidden. The player's colored
leading turn arc remains visible. Forecast helpers remain available for debugging.

Both pilots begin at 1,000 m MSL, opposite each other on a 28.65 m radius, flying
10 m/s and turning left at 20 degrees/s (18 seconds per orbit, about 19.6 degrees
bank). The thermal is 140 m **in diameter**. Its translation is independent of
airmass wind: the wind instrument correctly remains at 0 mph in this lesson.

The weather uses the scenario seed, advances only on fixed simulation ticks,
and stops when paused. Restarting the same seed repeats the same weather.
There is no additional altitude-dependent bend, diameter variation, texture,
or legacy random drift in this lesson. Strength variation means a percentage
of the initial **net core climb**, not a compounding percentage of its last value.
Work together uses a flatter interior and a smooth, sharper outer shoulder.
Initial net core climb is 6 m/s, with air lift of 7 m/s.
Inside 70% radius (49 m), lift decreases by 0.05 m/s per meter outward,
half the former 0.10 gradient. Beyond 49 m, a cubic Hermite curve steepens
the drop to zero air lift at 70 m, with continuous lift and slope at the join.
Initial net climb on the 28.65 m circle is approximately 4.57 m/s, versus 3.14
with the original linear profile; the flatter profile still improves off-center lift.
Weather scales this whole profile. Other lessons retain their existing profiles.

The friend flies an independent path, not a mirrored user position. Its own
filtered vario trend widens the circle (less turn rate) in improving lift and
tightens it in deteriorating lift. Steady vario leaves the ideal rate plus the
smaller cooperative corrections.
The relative-altitude derivative estimates whether the user is rising or falling.
For a better-climbing user, the NPC tightens when the user is inside its turn and
widens when the user is outside. This is solely a lateral-position cue: strongest
directly abeam, 70.7% strength on either 45-degree diagonal, and zero directly
ahead or behind. There is no ahead/behind correction to cancel it. The response
reverses when the user is falling. This climb cue does not read the core or the
user's controls/vario. A sustained altitude difference alone causes no climb bias.
The own-vario trend now takes priority: any individual partner cue opposing its
correction is suppressed. Thus worsening lift cannot be cancelled by a partner
request to widen; improving lift likewise keeps its own widening decision.
Within the vario-trend deadband, both partner cues are allowed. Agreeing cues add.
The thermal-boundary guard remains the highest-priority safety assist.

After combining the cues and limiting the requested rate to 12–28 degrees/s,
`wideningIncentiveScale = 0.5` halves any reduction below the ideal 20 degrees/s.
For example, an old request of 14 becomes 17 degrees/s; 12 becomes 16.
Tightening requests, response smoothing, and slew speed remain unchanged. Applying
this after combining cues preserves the old tightening behavior even when cues
oppose each other. The thermal-boundary guard bypasses this incentive scale and
retains the full 12–28 degrees/s range when needed for containment.

All three steering gains and their correction caps are now 3x their previous
values. Final turn limits, timing, own-vario priority and half-widening remain
unchanged; saturated or vetoed cues will not produce a full 3x turn change.
The cooperative climb correction is `clamp(9 * deadband(relativeClimb, 0.04) *
0.8 * insideUser, -7.5, 7.5) * rangeWeight` in degrees/s, subject to own-vario priority.
`insideUser` is a signed projection onto the turning-side lateral unit vector.
For a settled 1 m/s relative-climb advantage within 100 m, it requests +6.912 degrees/s
directly inside, +4.888 degrees/s ahead-and-inside or behind-and-inside, and zero
ahead/behind (before own-vario and guard corrections). There is still no timed or
90-degree committed maneuver. Response filters and the half-widening scale are unchanged.

A separate small spacing cue uses both pilots' bearings about the actual moving
thermal center. The desired separation is 180 degrees. Define `gap` as the forward
angular distance from NPC to user in the orbit direction: above 180 the NPC is
falling behind, below 180 it is catching up. The raw rate bias is
`3 degrees/s * sin(gap - 180 degrees)`, smoothed over 1 second. Near the target it
tightens when behind and loosens when catching up. The periodic curve is continuous
at the 0/360 wrap and neutral at exact overlap, where leading/trailing is ambiguous.
This is orbit-phase spacing, not the removed heading-relative ahead/behind cue.
It fades near the center (within 5 m, where angle is poorly defined), uses the same
100–250 m partner-range fade, and yields to a conflicting own-vario trend.
With steady vario and no climb cue: gaps of 135 / 180 / 225 degrees give settled
commands of approximately 18.94 / 20 / 22.12 degrees/s after half-strength widening.
Maximum spacing-only commands are 18.5–23 degrees/s. This is a bias, not
a guaranteed formation lock; the guard and lift-seeking can override it.

A separate dependable-partner assist now uses the actual moving thermal center
to keep the NPC's **position** between 20% and 70% of the thermal radius: 14–49 m
for this 140 m diameter thermal. These are not limits on the NPC's turn radius.
It predicts six seconds ahead, including turn-response limits and current thermal
translation. When the normal steering would enter a 4 m boundary buffer, it chooses
a safer turn command within the existing 12–28 degrees/s range. Normal vario/social
steering remains active when the projected path is clear. A hard radial clamp
removes any remaining out-of-band displacement after a physics tick, without
snapping the heading; this is a game assist, not a fully physical flight controller.
The player's flight is not constrained. The dashed forecast still shows the current
turn-rate arc, not future guard actions.

Guard controls: `thermalMinRadiusFraction = 0.20`,
`thermalMaxRadiusFraction = 0.70`, `thermalGuardMarginM = 4`,
`thermalGuardLookaheadS = 6`. Steering gains, slew limits, and thermal weather
coefficients are otherwise unchanged.
The NPC is a bounded heuristic, not guaranteed optimal or collision-proof.

The side view renders the accepted GLB model live, preserving its rounded cells,
traced white/black/lime texture, pod harness and three-tier suspension. Observer
position relative to the friend's heading determines yaw and elevation; airspeed
and turn rate determine bank. Directly opposite, same-altitude pilots see the
wing nearly side-on; other angles reveal more of its span. The view faces the user's
initial inside wingtip (heading minus 90 degrees for this left-turn lesson).
The viewing shoulder stays fixed even when steering reverses; it does not flip
180 degrees at zero turn rate. A bounded head turn follows the friend using
`headYaw = 45 * sin(offWingtipBearing)`, continuous through the unseen rear wrap.
The horizontal field is 120 degrees around that head direction; the friend fades
over its final 8 degrees and leaves view beyond the limit (about 103 degrees off
abeam with the defaults). There is no horizontal edge clamp. Head turning also
moves the panorama and foreground wing consistently. Vertical placement maps
true pilot-to-pilot elevation:
`atan2(friend altitude - user altitude, horizontal separation)`. The top of the
viewing area is +90 degrees, the middle 0, the bottom -90.
The pilot/harness anchor, not the center of the bitmap, follows the scaled bearing.
Vertical screen placement uses a center-expanded angular scale:
`y = (1 - atan(focus * elevation / 90) / atan(focus)) / 2`.
The default focus of 11.895 gives approximately 8 times the motion around the horizon,
smoothly compressing toward the unchanged +90/-90 endpoints. Focus 0 restores
linear placement. This is visual exaggeration only: the actual view angle used to
orient the 3D model, flight physics, NPC control and background horizon are unchanged.
Apparent sprite size uses full 3D pilot-to-pilot distance, not just horizontal
range: `heightFraction = 0.62 * sizeScale * 57.3 / distanceMetres`. Twice the range
halves the size; half the range doubles it. The broad 0.02–1.8 limits prevent
extreme sizes without capping growth at the starting range. Close models can
extend beyond the viewport. This remains an illustrative sprite projection, not
a calibrated physical angular-size camera.
The user's inside wingtip is an illustrated foreground overlay with a shaded
blue/slate underside, muted curved fabric panel, fuller leading-edge shoulder,
rounded tip and darkened hem. Bowed ribs sweep upward around the airfoil rather
than forming perfectly horizontal bars. Twelve low-contrast shading bands per
cell follow those same curves, giving a soft inflated-fabric appearance rather
than a glossy highlight. It uses the user's coordinated-turn bank, not the NPC's.
Its nominal level elevation is 30 degrees; increasing bank lowers it through the
same expanded angular scale. It is painted after the friend so it can occlude them.
Its U-shaped silhouette hangs vertically from above, with curved pillowed
cells and a rounded lower tip. Cells are twice the original thickness (5.4% of
panel height), anchored to the moving tip so the ribs travel with the fabric.
Additional cells enter from above as the wing lowers. Its bottom
reaches approximately -1.6%, 12%, and 34% down the panel at turn rates 0, 20, and
30 degrees/s respectively. The wingtip also slews opposite the bounded head turn:
looking right moves the near wing left and vice versa, limited
to 95% of panel width (about 4x the previous travel), allowing it to leave either
edge completely. The 8x elevation scale is unchanged.
Visibility now requires the NPC to be on the same side as the user's bank:
right-side NPC + right bank shows the right tip; left-side NPC + left bank shows
the left tip. Level flight and opposite-side banking hide both tips. Directly
ahead/behind or with zero horizontal separation, neither side is selected.
The level-flight tip sits just above the viewport, including its rounded hem.
Banking lowers fully opaque fabric into view naturally, with no opacity fade.
Mirrored shape, cell movement and head-turn slew remain unchanged. These visibility rules do not
change the limited side-view camera or force an out-of-view NPC back into view.
This is a visual
canopy cue, not an additional aerodynamic or collision model.
There is no meter-to-pixel gain, vertical travel clamp, or 65-degree elevation cap.
The canopy may clip at the poles. There are no angle markings, altitude line,
bearing marker, relative-altitude/distance band, or climb-status hints in the view.
The background is uniformly scaled and cropped around its approximate distant
skyline (61% down the source image), aligning that skyline with the panel midpoint
and an equal-altitude pilot. The original image and physical elevation are unchanged;
mountain peaks naturally vary slightly around this reference horizon.
Distance affects apparent
size, with readability limits. A clearly labelled static model-rendered fallback
is used only while loading or if WebGL is unavailable. No external CDN is needed.

## Central tuning parameters

All new defaults are in `friend-training.js`, `FriendTraining.config`.

### Thermal

| Key | Default | Meaning |
|---|---:|---|
| `diameterM` | 140 m | Thermal diameter, not radius |
| `initialCoreClimbMps` | 6 m/s | Initial net climb at core |
| `thermalInteriorDropMps` | 3.5 m/s | Interior air-lift loss per normalized radius at initial strength (0.05 m/s per meter) |
| `thermalEdgeStartFraction` | 0.7 | Start of the smooth, steeper outer drop-off (49 m) |
| `sinkMps` | 1 m/s | Both pilots' base sink |
| `driftMaxMph` | 1 mph | Maximum translation speed (0.44704 m/s) |
| `driftRampS` | 10 s | Linear speed ramp from zero |
| `driftInitialBearingDeg` | 0 degrees | North; bearings increase clockwise |
| `driftBearingRateMaxDps` | 10 degrees/s | Maximum heading wander rate |
| `driftBearingAccelerationDps2` | 5 degrees/s² | Smoothness of heading-rate changes |
| `driftRetargetS` | 2 s | New seeded random heading-rate target |
| `strengthRangeFraction` | 0.25 | Range 75–125% of initial core climb |
| `strengthRateMaxFractionPerS` | 0.02/s | Maximum change of 2 percentage points/s |
| `strengthRetargetS` | 3 s | New seeded random strength-rate target |
| `strengthRateFilterS` | 1.5 s | Strength-rate smoothing time constant |

### Initial flight and NPC steering

| Key | Default | Meaning |
|---|---:|---|
| `airspeedMps` | 10 m/s | Airspeed of both pilots in this lesson |
| `idealTurnDps` | 20 degrees/s | Nominal turn-rate magnitude |
| `turnDirection` | -1 | Left (-1), right (+1) |
| `initialSeparationDeg` | 180 degrees | Initial azimuth separation |
| `varioFilterS` | 0.6 s | NPC vario smoothing |
| `trendFilterS` | 0.8 s | Smoothing of vario derivative |
| `trendDeadbandMps2` | 0.015 m/s² | Ignore tiny vario changes |
| `trendGain` | 72 | Turn correction degrees/s per m/s² beyond deadband |
| `trendMaxCorrectionDps` | 18 degrees/s | Limit of own-vario steering contribution |
| `relativeClimbFilterS` | 0.25 s | Smoothing of observed relative climb |
| `relativeClimbDeadbandMps` | 0.04 m/s | Ignore nearly matched climb |
| `socialGain` | 9 | Turn correction degrees/s per m/s relative climb before directional weighting |
| `socialMaxCorrectionDps` | 7.5 degrees/s | Limit of cooperative climb contribution before vario-priority veto |
| `socialInsideTurnWeight` | 0.8 | Multiplier of the sole inside/outside directional cue |
| `wideningIncentiveScale` | 0.5 | Scale of net widening below ideal; 1 restores symmetric response, tightening unchanged |
| `separationTargetDeg` | 180 degrees | Desired forward orbit-phase gap, measured about the moving thermal center |
| `separationMaxCorrectionDps` | 3 degrees/s | Maximum spacing bias before half-strength widening and priority veto |
| `separationFilterS` | 1 s | Smoothing time for the small spacing bias |
| `separationMinRadiusM` | 5 m | Suppress unreliable angular cues near the thermal center |
| `socialFullRangeM` | 100 m | Full social response within this distance |
| `socialCutoffRangeM` | 250 m | Response fades to zero by this distance |
| `minTurnDps` | 12 degrees/s | Widest allowed NPC turn (~47.75 m radius) |
| `maxTurnDps` | 28 degrees/s | Tightest allowed NPC turn (~20.46 m radius) |
| `turnResponseS` | 0.2 s | Response smoothing toward commanded turn |
| `turnSlewDps2` | 20 degrees/s² | Maximum rate of turn-rate adjustment |
| `turnSinkStartDps` | 20 degrees/s | NPC turn sink penalty starts here |
| `turnSinkFullDps` | 30 degrees/s | Full NPC turn sink penalty here |
| `turnSinkMaxMps` | 0 m/s | Bank/turn sink disabled for both pilots in Work together |
| `gravityMps2` | 9.80665 m/s² | Coordinated-turn bank calculation |

The player and NPC share these turn-sink parameters in this lesson. Added bank/turn
sink is disabled; both still have the normal 1 m/s base sink. Other stages retain
their existing penalty calculation. The start/full-rate settings are dormant while
the maximum penalty is zero.

### Side-view presentation

Top-view forecast controls: `topViewForecastSeconds = 6`,
`topViewForecastStartSeconds = 0.8`, `topViewForecastStepSeconds = 0.1`.
These affect the drawing only; steering coefficients are unchanged.

| Key | Default | Meaning |
|---|---:|---|
| `sideViewMaxFps` | 30 | Maximum offscreen 3D render rate |
| `sideViewPixelRatio` | 2 | Render resolution: 256 × ratio, square |
| `sideViewFovDeg` | 32 degrees | Perspective field of view |
| `sideViewMinSizeFraction` | 0.02 | Minimum sprite size / spherical viewing-area height |
| `sideViewMaxSizeFraction` | 1.8 | Close-range safety cap; permits clipping beyond the viewport |
| `sideViewReferenceSizeFraction` | 0.62 | Sprite size / panel height at reference distance and scale 1 |
| `sideViewSizeScale` | 1 | Overall apparent-size multiplier |
| `sideViewReferenceRangeM` | 57.3 m | Reference viewing distance (starting separation) |
| `sideViewPanoramaHorizonFraction` | 0.61 | Source skyline height used to center the background crop |
| `sideViewElevationFocus` | 11.895 | Horizon-centered expansion; 0 = linear, default = ~8x central sensitivity |
| `sideViewHeadYawMaxDeg` | 45 degrees | Maximum head turn from the initial inside wingtip |
| `sideViewHorizontalFovDeg` | 120 degrees | Visible horizontal field around the head direction |
| `sideViewEdgeFadeDeg` | 8 degrees | Soft disappearance zone just inside each viewing limit |
| `sideViewWingtipLevelElevationDeg` | 30 degrees | Foreground wingtip elevation at level; subtract the user's bank |
| `sideViewWingtipSlewFraction` | 0.95 | Maximum sideways shift / panel width at full head turn; may leave the view |
| `sideViewWingtipCellHeightFraction` | 0.054 | Cell thickness / panel height; ribs stay anchored to the moving tip |
| `sideViewWingtipLevelClearanceFraction` | 0.016 | Level tip anchor sits 1.6% of panel height above the top, clearing the rounded hem and stroke |

Further visual controls in `friend-renderer.js`: lighting, matte material
roughness 0.88, neutral tone mapping/exposure 1, line color #465054, and fit padding
8%. Panel sizes in `drawFriendTogetherSideView`: desktop width 270 logical px,
mobile 230, height 250–360. The model proportions/cell bulges are authored in the
GLB; regenerate that asset from the Geometry Lab when changing the model itself.

### Existing lesson controls in index.html

- Scenario seed: existing settings seed; controls the deterministic random walk.
- Starting altitude: 1,000 m MSL in `freshState`.
- Physics timestep: 1/60 s (`dt`).
- Required climb per step: 500 m (`goalClimbM`).
- Thermal, rings, and flight track are hidden for this lesson.
- User turn-rate limit: 30 degrees/s, with existing keyboard/drag steering.
- Airmass wind: 0 mph, diameter/altitude strength variation and texture: 0.
- Work together checks lift, relative altitude and 20 m 3D clearance. Harder
  conditions uses the same checks until the newcomer joins, then checks lift
  and 20 m 3D clearance instead.

## Assets and verification

`friend-pilot.glb` is the neutral exported Geometry Lab model, with embedded skins.
`friend-pilot.png` is its 20-degree rendered fallback. Three.js 0.180.0 and its
GLTF loader/geometry helper are vendored under `vendor/three` with the MIT license.

Tests: `node --test website/scripts/thermal-friends.test.mjs` from the repo root.
They cover exact initial geometry, weather determinism and rate bounds over
20 simulated minutes, vario responses, direction of social orbit translation,
relative camera geometry, and an eight-minute independent NPC simulation.

Local standalone preview (from repo root):
`python -m http.server 4322 --bind 127.0.0.1 --directory website/public`

Open `http://127.0.0.1:4322/labs/thermal-training/?lesson=work-together`.
This is local-only; no production publication or firmware changes are needed.
