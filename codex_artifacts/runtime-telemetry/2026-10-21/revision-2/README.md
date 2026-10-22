# Schema 2 instrumentation validation

[Simulator report](simulator.html) demonstrates the revised page and button/recovery sections.
The integration executable exercises the real scheduler and display paths, holding each of seven
non-menu pages for 60 virtual seconds, then visiting a menu and injecting 10/20 ms loop stalls
during a held button. The report is a smoke assessment requiring review, not a release baseline.

- All seven page contexts have at least 120 normal render samples; all 100 slots retain more
  than 400 normal observations. Menus and overlays do not count as primary page renders.
- Button and recovery samples are excluded from normal grid/page statistics, while recovery
  delays, missed deadlines, skipped ticks and barometer timing remain independently visible.
- The capture allocation is 22,320 bytes in this build. It exists only after capture starts;
  no page/recovery arrays are allocated with capture off. Successful export frees storage.
- The [device flight-page report](device-flight.html) now provides a 173.16-second schema 2
  capture across five flight pages. Historical schema 1 captures in the parent folder remain
  preserved and clearly marked legacy; their aggregates cannot supply the new measurements.
- Raw capture, metadata, report JSON, executable hash and source manifest are retained here.
  The executable hash identifies `runtime_capture_integration_test`, not the normal simulator
  launcher. The version string is the repository placeholder; the explicit variant identifies
  the simulated board.

Checks: firmware release build, recorder and lifecycle checks (including Developer mode off),
all-page/scheduler integration, display/settings regressions, ASan/UBSan accounting, 16 report
tests and report structure/provenance checks. Real driver recovery, GPS/BLE/IGC activity and
additional operating modes still require further hardware assessment.

The updated Leaf 3.2.8 release image was flashed to both OTA slots and verified by esptool.
Its hash and build identification are in `firmware-build.json`. The device reset after flashing;
the operator subsequently completed the five-page capture summarized below. Reported static RAM
is 103,556 bytes, unchanged from the preceding stage; page/recovery buffers are capture-only.

## Device five-page assessment

The operator visited five flight pages for about 30 seconds each, saved `/diagnostics/runtime_2.json`,
and exposed the SD card in charging mode. The copied raw file, metadata, HTML/JSON report and IGC
summary are retained alongside the simulator evidence. Raw telemetry and the flashed firmware
hash match their recorded provenance. No previous compatible approved baseline exists.

| Page | Normal renders | Mean render ms | Max render ms | Max occupied slot ms |
| --- | ---: | ---: | ---: | ---: |
| Thermal Track | 64 | 8.003 | 8.788 | 9.271 |
| Navigate | 72 | 6.923 | 7.203 | 8.089 |
| User | 52 | 6.037 | 6.664 | 7.242 |
| Basic | 52 | 5.731 | 6.201 | 7.018 |
| Thermal Core | 60 | 5.114 | 5.271 | 5.886 |

Debug pages were not visited. All five measured pages had zero normal render-slot deadline
crossings. Thermal Track left only 0.729 ms of headroom at its largest observed occupied slot.
A different history, navigation state or radio workload could cost more; these are observed maxima.

- Normal grid: 16,840 observations, at least 165 per slot; zero occupied durations over 10 ms
  and zero coalesced ticks. There were 176 deadline crossings (about 1.05%), concentrated in
  slots 76–83, with maximum completion delay 1.795 ms. The retained worst slot began only
  11 microseconds late; assigned tasks ended after about 0.55 ms and GPS work completed late.
  Arrival timing, parsing, blocking and preemption are not separated; this is a contributor
  trace rather than proof of a CPU saturation fault.
- The IGC slot's maximum occupancy was 8.798 ms. Normal slot 36 started 5.172 ms late and
  first called the barometer 5.216 ms late. Its cause is not established by the retained trace.
- Buttons: 387 excluded slots; peak occupied time 25.025 ms. Recovery: 84 separate slots,
  six deadline crossings, four coalesced ticks, max start delay 8.857 ms and barometer first-call
  delay 8.914 ms. Do not treat exclusions as proof that hardware recovers correctly.
- Internal heap: 33,888 to 33,940 bytes (+52), sampled minimum 33,880; boot-lifetime minimum
  27,336; largest free block 13,812. Zero allocation failures. Loop/BLE stack headroom was
  8,308/2,992 bytes. These values include the 22,320-byte capture allocation; successful export
  releases that buffer. Post-export free heap was not directly sampled.
- Observed clock-pair maximum 1 microsecond, bookkeeping 259 microseconds, memory sampling
  659 microseconds. These are elapsed maxima, not an isolated or complete overhead estimate.
- BLE was enabled; client connection and traffic are unverified. The latest IGC artifact has
  301 valid fixes at exactly one-second intervals, with no gaps. Exact overlap with monotonic
  telemetry time is not encoded, so individual post-button flight-data continuity is unproven.

This is useful manual assessment evidence requiring review, not release approval or a sustained
heap/functional recovery test. No SD-card files were changed during retrieval. Position data
is not included in the retained IGC summary.
