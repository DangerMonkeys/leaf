# Firmware route guidance

The device and host simulator compile the same fixed-storage primal-dual solver
(`src/vario/navigation/route_optimizer.h`) and cooperative guidance service
(`src/vario/navigation/route_guidance.cpp`). The original desktop lab remains
available independently for comparisons and alternative margin policies.

## Default behavior and diagnostics

Optimization is automatic for eligible routes. There is no toggle. Settings
from the experimental build do not affect guidance. Original route coordinates
are preserved, and no optimized plan is stored on SD.

In Developer mode, use `GET /api/debug/route-optimizer` to read status, fixed workspace size, maximum solver
iteration/service/update durations, per-fix work, publication count, skipped
scheduler opportunities, free/internal minimum/largest heap and loop-stack
headroom. The endpoint uses temporary response storage only when requested;
there is no flight telemetry ring or dynamic allocation in the solver/service.
`max_publication_latency_ms` measures assembly of one complete plan snapshot,
not the time to mathematical convergence. Additional counters distinguish:

* `max_geometry_us`, `max_publish_point_us` and `max_refresh_us`: longest
  geometry unit, publication unit (including final refresh), and final navigation
  refresh (a subset of publication). These overlap; do not sum their maxima.
* `slices_over_budget`: cooperative units longer than the 2000 us reserve.
  This counts overruns; the admission check cannot preempt a unit in progress.
* `max_request_latency_ms` / `last_request_latency_ms`: oldest pending fresh-fix
  or route-change request to the next accepted plan. Further fixes do not reset
  a pending request. This is responsiveness, not convergence or a guarantee
  that the accepted plan was optimized from the newest fix.
* `max_first_plan_latency_ms`: route restart or waypoint advance to its first
  accepted feasible plan. `pending_request_age_ms` exposes unfinished work.
* `plan_source_age_ms`: current age of the GPS epoch used when assembly of the
  accepted snapshot began. New fixes immediately reprice its distances/bearings,
  without pretending the frozen targets have been recomputed from that new fix.
* `phase`, `route_capacity`, `remaining_points`: current work and capacity.

Counters are collected only while Developer mode is on. Enable it before the workload; use fresh boots for comparisons.
The 20 route references are a shared pool across loaded routes; saved SD files
remain independent and loading one saved route replaces the pool. Files with
more than 20 route references are rejected by saved-route import/load and
GPX/CUP loading. Individual waypoint capacity stays 120.

Timings in the simulator use virtual
time and are not ESP32 performance measurements.

## Runtime design

* Capacity is 20 route points (the same limit as Leaf route storage); execution loops use the remaining
  count. One persistent workspace has a compile-time ceiling of 1536 bytes.
  The original 40-point workspace was 2200 bytes on ESP32; reducing capacity
  alone saves 880 bytes, with 120 additional bytes saved in route-reference storage.
  New counters add a small fixed cost; `workspace_bytes` reports the complete
  current workspace, excluding call-stack and math-library use.
* Initialize with 64 improvement iterations; budget 16 on a fresh GPS position
  epoch. GGA/RMC receipt times for the same UTC epoch do not double that budget.
  Fresh fixes extend a bounded remaining budget, never accumulate a work queue.
* Geometry construction, each solver iteration, and each guidance-plan point are
  separately scheduled units. An initial feasible center-based plan is published
  before convergence; improved plans publish after groups of 16 iterations.
* TaskManager offers one unit in each light 10 ms slot ending in 1/3/4/6/8,
  after mandatory work and GPS UART servicing, only when at least 2 ms remain.
  No new RTOS task/stack, catch-up burst, UART callback solver, or blocking solve.
  Runtime labels for these opportunities are `base_route`.
* New fixes immediately reprice the accepted path and its bearings. Solver points
  stay frozen while a snapshot is assembled. All fields use the accepted
  snapshot; publication refreshes guidance without invoking waypoint sequencing.
* Real cylinder achievement still belongs to Navigator. On advancement the
  optimizer shifts its state; activation/restart, route edits, cancellation,
  GPS loss invalidates old results. Developer mode does not affect guidance.

## Geometry and semantics

The nominal optimizer uses actual cylinder radii. Afterwards, inward aim points
use a 20 m margin at tangent approaches, tapering in squared-cosine space to zero
at 45 degrees away from tangent. Radii **below** 20 m aim at the center. Exactly
20 m can retain an unbuffered direct approach. A 5 cm numerical inset makes
boundary targets reachable with the native strict `< radius` test; it is separate
from the angle-dependent pilot margin.

The local spherical azimuthal-equidistant projection uses the same 6372795 m
sphere as TinyGPSPlus navigation. It handles high latitudes and longitude wraps.
Geometry is accepted only within 200 km of the activation origin and with a
conservative disk distortion bound of 0.5 m. Each adjusted geographic target is
also corrected inward against the actual spherical radius. Original WGS84/FAI
metadata is preserved; the experiment follows Leaf's existing spherical
sequencer rather than introducing competition-scoring earth-model semantics.

Cylinder distance is the analytic first crossing of the actual spherical cylinder
along the great-circle segment toward the aim point. A valid zero is displayed
as zero. Sequencing happens on the first usable fix strictly inside the actual
radius while logging is ready, so the LCD can switch directly to the next leg
without displaying zero for the previous one. Time, required glide, arrival
altitude and progress use the boundary distance. Goal distance follows adjusted
intermediate aim points and stops at actual goal-cylinder entry. Center-specific
waypoint fields retain their existing meaning. The solid pointer aims at the
active adjusted point; the hollow pointer bears from the pilot toward the
following adjusted point and disappears for the final waypoint.

Fallback to existing navigation occurs for unsupported timed starts, start-speed
section roles, goal lines, projection extent/distortion, invalid data or failed
geometry. A diagnostic status identifies the reason. This feature provides cylinder-entry navigation, not competition task scoring.

## Hardware test summary

On Leaf 3.2.5 running `0.2.4-ba7+h3.2.5`:

| Workload | Observed result |
| --- | --- |
| Eight-point travel test, Bluetooth off | GPS fixes entered all eight cylinders in order. 414 plans, zero fallbacks; longest solver slice 1.844 ms. |
| Stationary solve with 20 points remaining, FANET transmitting and a phone receiving Bluetooth data | 85 plans, zero fallbacks; first plan within 848 ms, longest request to publication 1.476 s. Three slices exceeded the 2 ms admission reserve; the longest was 2.209 ms. |

The optimizer used 1,376 bytes of fixed workspace. The stationary run used
the lightweight counters without the full runtime capture, so it does not
establish whole-system deadlines or Bluetooth-on heap headroom. A recurring
IGC logging-slot stall in the travel captures was separate from optimizer
slices; the exact blocking operation was not isolated.

The automatic-guidance follow-up (`0.2.4-b14+h3.2.5`) was flashed and
verified to reboot. Simulator regressions cover automatic guidance without
Developer mode, boundary zero and leg advancement, stale GPS recovery, 20/21
point imports, and unsupported tasks. The timing measurements above are from
the earlier test build, not a fresh capture of the PR head.
