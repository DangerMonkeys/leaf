# Schema 3 GPS sentence profiling

[Simulator report](simulator.html) demonstrates sentence type/checksum summaries, exclusive
processing stages, complete-line dispatch phase and retained deadline crossings. Its data comes
from the integration-test executable, not an approved operational workload or real device.

- Uses the actual GPS parser, LC86G UART driver and message bus with host HAL.
- Exercises fragmented valid GGA, a rejected checksum, ordinary injected playback and a
  deterministic 20 ms virtual stall during a valid dispatch. The stall is not ESP32 CPU cost.
- Includes normal page timing and separate button/recovery data. Existing simulator regressions,
  pure accounting under ASan/UBSan, lifecycle/thread ownership and 21 report tests pass.
- Capture allocation is 22,496 bytes: 176 bytes more than schema 2. Recent slot history is eight
  slots and memory history sixteen samples; full-run aggregates, endpoints/minima and retained
  worst slot/predecessor records remain. GPS retains eight recent sentences and four crossings.
- New buffers remain capture-only. Disabled hooks read the capture flag without collecting
  timestamps; the read path avoids new checks on every empty idle poll. Only small counters
  and owner state are permanent. The internal memory reserve is unchanged.
- GPS processing includes elapsed blocking/preemption on hardware; the simulator uses host
  thread CPU. First-read timestamps observe firmware ingestion rather than wire arrival.
- Raw evidence, metadata, report JSON and executable/source identification are retained here.
  The executable hash identifies `runtime_capture_integration_test`. The firmware version
  string in the simulator is the repository placeholder; the build variant identifies the board.

## Hardware findings

[Hardware report](device-gps.html) and [raw capture](device-gps-raw.json): 80.691 seconds from
`/diagnostics/runtime_3.json`, on the exact flashed schema 3 image in `firmware-build.json`.
The source manifest matches all captured source files. Static RAM is 103,564 B, 8 B above
schema 2; active capture allocation is 22,496 B, released after successful export.

- All **100 normal slot deadline crossings** are in slots **80-89**. There are zero normal
  accumulated-duration overruns and zero normal coalesced ticks.
- **1,051/1,057 sentence dispatches (99.43%)** occur in slots 80-89, containing 99.81% of normal
  GPS work. Counts: GGA/RMC/VTG/GLL 81 each; GSV 405; GSA 243; Other 85. All pass the parser
  checksum. This pattern supports a recurring GPS burst; checksum acceptance alone does not
  establish a valid position fix.
- Active UART reads account for **77.7%** of measured GPS sentence processing. Parsing is 5.5%,
  immediate GPS updates 2.7%, reading callbacks 5.8%, BLE enqueue 1.7%, mutex waits together
  0.9%, remaining processing 5.7%. These are elapsed times including blocking/preemption and
  instrumentation, not isolated CPU instruction costs. Scheduled navigation/glide work remains
  in slots 09/59.
- Full-run sentence crossings: GSV 29, GSA 14, Other 10, GGA 9, RMC 7, VTG 4, GLL 2. All four
  retained crossings are GGA. Their detailed histories establish those incidents' mechanism,
  not the history of every crossing.

Worst retained normal incident: slot 80, tick 19511. Offsets from intended start:

| Event | Offset |
| --- | ---: |
| Slot actually starts | +0.003 ms |
| Assigned task block ends | +0.861 ms |
| GGA UART read begins | +9.887 ms |
| Slot deadline | +10.000 ms |
| Complete line ready | +11.389 ms |
| Sentence dispatch finishes | +11.825 ms |
| Last useful work ends | +11.874 ms |

The preceding slot finished useful work before its deadline. This slot was not pushed late by
that predecessor. Reading began with only 0.113 ms remaining and had already crossed the
boundary before the line was complete; dispatch added 0.436 ms. GGA processing took 1.912 ms,
including 1.492 ms of UART reading. Assigned-task plus GPS occupancy was only 2.820 ms.
This supports **late-started reading/dispatch running beyond the boundary**, rather than a
large calculation consuming the whole slot.

Sentence crossings (75, including interactions) and normal slot crossings (100) differ in
coverage and units. Multiple sentences can share a slot; a UART read chunk can cross a deadline
without completing a sentence in that slot. The scheduler checks ticks between `readLine`
calls; reading and complete-line dispatch presently have no internal deadline check. A future
change could bound reading and defer dispatch when too little budget remains, preserving
partial lines and validating UART-buffer, sensor and IGC continuity. No scheduling fix was
made for this capture. Firmware read timestamps are not wire-arrival timestamps, and logical
phase can move after coalesced ticks, so slot numbers need not match the previous run.

Other observations:

- Internal free heap: **33,708 B at both endpoints**; sampled minimum 33,596 B; largest block
  13,812 B; zero allocation failures. These include capture allocation. A short stable run
  does not establish a long-term leak-free baseline.
- Navigate: 149 normal renders, mean 6.854 ms, maximum 7.393 ms, zero normal render-slot
  deadline crossings. Other flight pages were unvisited.
- Separately excluded: 175 button slots (peak 34.756 ms occupied), 33 recovery slots, one
  recovery deadline crossing and three coalesced ticks. Recovery start delay peaked at
  8.693 ms and barometer first-call delay at 8.760 ms; functional resilience remains unproven.
- BLE enabled; connected-client traffic unverified. FANET and diagnostic SD logs disabled.
  Latest IGC contains 107 valid fix records at 1 Hz without gaps; exact telemetry overlap is
  not encoded. Only a position-free [IGC summary](device-gps-igc-summary.json) is retained.
- No omitted async/nested dispatches, partial ingestion, UART resets or unassigned sentences.
  GPS profile-record bookkeeping peaked at 43 microseconds. Full overhead requires independent
  calibration. The report has 100 slot cells and 100 GPS phase bars; embedded data and raw,
  firmware and source hashes were checked.

The earlier [five-page report](../revision-2/device-flight.html) preserves its original schema 2
capture. Schema, duration and workload differ; this is **not a compatible regression comparison**.
This short manual diagnostic assessment requires review and is not release qualification.
