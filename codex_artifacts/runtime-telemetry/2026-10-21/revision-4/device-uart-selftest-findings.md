# Character-yield capture with final manual self-test

[Full report](device-uart-selftest.html) | [Raw capture](device-uart-selftest-raw.json) |
[Metadata](device-uart-selftest-metadata.json)

The physical Leaf 3.2.8 capture lasts **116.391 seconds**. The operator reports approximately
90 seconds of ordinary operation followed by approximately 30 seconds of manual self-test.
This is useful diagnostic evidence, but **not a clean comparable baseline**. The exact segment
boundaries are absent, and per-slot aggregates cannot be split retrospectively. No data was
removed, estimated as a replacement, or labeled as a clean 90-second subset.

## GPS observations

| Metric | Before: revision 3 | After: this mixed capture |
| --- | ---: | ---: |
| Complete sentence dispatches | 1,057 | 1,525 |
| Sentence deadline crossings | 75 (7.10%) | 41 (2.69%) |
| Maximum sentence lateness | 1.825 ms | 0.516 ms |
| Mean measured processing per sentence | 1.354 ms | 1.348 ms |
| Checksum rejected / UART assembly resets | 0 / 0 | 0 / 0 |
| Capture duration | 80.691 s | 116.391 s |

The lower sentence crossing fraction and maximum lateness are encouraging. They do not isolate
the causal improvement: the captures differ in duration, workload, GPS phase and satellite data,
and this one includes self-test. Stages are elapsed time including preemption and instrumentation.
The goal was earlier yielding rather than reducing the amount of GPS work, so similar mean
processing costs are consistent with the change.

A retained GSV event at capture **+8.259 seconds**, before the reported self-test period, has
its complete line ready at slot **+9.973 ms**, and dispatch finishes at **+10.417 ms**. This is
the explicitly allowed behavior: reading can yield between characters, while a completion
callback begun near the boundary remains inline. Other retained crossings reach line-ready
+10.057 ms and dispatch-end +10.516 ms; character checks do not preempt an individual UART call.
The exact first-read-to-ready span can include prior slots and idle fragment gaps.

The phase chart spans logical slots 51-70, versus 80-89 before. **This does not prove that each
GPS burst spread across twice as many slots.** Twelve coalesced ticks across normal/recovery
intervals can shift the logical schedule index relative to the receiver's cadence. The bounded
records cannot reconstruct every burst before and after those shifts.

## Self-test and other timing

- The self-test log reports **ALL TESTS PASSED**, including GPS serial/fix. It records one
  stationary IMU measurement retry and saved gyro/flat bias values. It has no monotonic boundaries.
- Normal aggregates contain **202 crossings in 11,191 slot observations**, three occupied-duration
  overruns, and six coalesced ticks. Before, these were 100/7,857, zero and zero. These whole-run
  figures are not a valid performance regression comparison because self-test work is included.
- The worst retained assigned-task block is **77.004 ms** in slot 27 at capture **+96.909 s**,
  consistent with the reported self-test period. It starts only 6 microseconds late, so the long
  work occurs in that slot. Its subtask is not individually profiled, so the log does not prove
  which self-test operation took the time. The next slot records six coalesced ticks.
- Other normal occupied-duration peaks above 10 ms occur in slots 55 (10.282 ms) and 97
  (14.652 ms); their incident timestamps are not retained, so their segments cannot be assigned.
- Button/recovery totals are separate: 357 button slots, 78 recovery slots; recovery includes
  five crossings, two duration overruns and six more coalesced ticks. Retained recovery peak
  occurs at +84.520 s with a self-test display context in its predecessor. No functional
  sensor-recovery conclusion follows from the exclusions.
- Navigate has 158 normal renders, mean **6.751 ms**, peak **7.665 ms**, and zero render-slot
  deadline crossings. Other flight pages were unvisited. Menu/self-test overlays do not count
  as flight-page render samples; self-test tasks still contribute to slot timing.

## Memory and continuity

Internal free heap starts at **33,712 B**, ends at **33,304 B** (408 B lower), and has a sampled
minimum of **32,860 B**. Largest free block remains **14,324 B**; allocator failures are zero.
Loop/BLE stack headroom remains 8,380/2,992 B. These include the active 22,496 B capture buffer.
The self-test opens/logs files, displays pages and updates saved state; the mixed lifecycle means
this endpoint difference is **not evidence establishing a leak**, nor can it be dismissed without
an equivalent clean repeat. The boot-lifetime free-heap low watermark is unchanged within capture.

The latest [IGC summary](device-uart-selftest-igc-summary.json) contains **144 valid fix records**
with **one two-second interval**, all other intervals one second. Exact capture overlap is absent,
so this gap cannot be attributed to self-test or the UART change. Zero parser rejects/assembly
resets does not prove no bytes were dropped: hardware receive-overflow counters are not recorded.
BLE is enabled but connected-client traffic is unverified; FANET and diagnostic logs are disabled.

## Next useful capture

Repeat approximately **80-90 seconds on Navigate**, matching GPS/BLE/IGC settings, then STOP and
Save Timing without entering self-test. This will separate ordinary GPS-boundary behavior and
heap stability from self-test effects. No firmware changes are needed to repeat the run.
