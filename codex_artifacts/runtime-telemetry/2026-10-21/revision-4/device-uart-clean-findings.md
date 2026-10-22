# Repeat timing capture after UART character yielding

[Full report](device-uart-clean.html) | [Raw evidence](device-uart-clean-raw.json) |
[Metadata](device-uart-clean-metadata.json) | [Position-free IGC summary](device-uart-clean-igc-summary.json)

The operator repeated the Navigate-page timing assessment without the previously accidental
manual self-test. The saved interval is **117.461 seconds**, despite the approximately 90-second
operator estimate. The entire saved interval is retained. There are at least 114 normal samples
in every slot and 225 normal Navigate renders. The binary/source hashes match the installed
character-yield build; no firmware change or reflash was performed for this report.

## GPS result

| Metric | Before yielding: revision 3 | Repeat after yielding |
| --- | ---: | ---: |
| Duration | 80.691 s | 117.461 s |
| Completed GPS sentences | 1,057 | 1,657 |
| Sentence crossings | 75 (7.10%) | 32 (1.93%) |
| Maximum sentence lateness | 1.825 ms | 0.354 ms |
| Mean processing per sentence | 1.354 ms | 1.305 ms |
| Normal slot observations | 7,857 | 11,574 |
| Normal slot crossings | 100 (1.27%) | 156 (1.35%) |
| Maximum normal slot lateness | 1.874 ms | 0.386 ms |
| Normal accumulated-duration overruns | 0 | 0 |
| Normal coalesced timer ticks | 0 | 0 |

This supports substantially **shallower GPS-related deadline crossings**, rather than their
elimination. The normal slot crossing fraction is similar, while the sentence crossing fraction
and worst lateness are lower. These are observations, not a controlled percentage improvement:
duration, GPS phase and satellite-message mix differ. The report correctly withholds the formal
baseline comparison for different scenario/duration.

All 1,657 sentences passed parser checksums. Per-type crossings are GSV 14, GLL 6, RMC 5, GSA 4,
VTG 2, Other 1 and GGA 0. There are no reported UART assembly resets, missing parser results,
nested/asynchronous omissions, unassigned or partial-ingestion sentences. Hardware receive
buffer overflow is not recorded, so these counters cannot prove every byte was received.
Active UART reads remain 77.9% of measured GPS processing elapsed time; they are not isolated
CPU costs. The GPS profiler's record bookkeeping peaks at 38 microseconds.

The retained worst normal event is slot 70 at capture +7.949 s. The slot begins 0.154 ms late;
its GSA line is ready at +10.015 ms and sentence dispatch ends at +10.354 ms. Last useful work
ends at +10.386 ms. An individual UART read and sentence callbacks remain non-preemptible by
this cooperative timer check, as requested. Another retained sentence is ready at +9.927 ms
and ends at +10.338 ms. The earlier predecessor itself ends +0.130 ms beyond its deadline,
showing the distinction between inherited start delay and work extending the current slot.

GPS dispatch is concentrated in logical slots 68-77, with four Other dispatches in slot 60;
99.88% of normal GPS work is in slots 67-77. Receiver phase and coalesced ticks can change the
logical indices between runs. A phase chart alone does not establish that each burst became
wider because of character yielding.

## Memory, display and IGC

- Free internal heap **33,516 -> 33,548 B (+32 B)**; sampled minimum **33,216 B**.
- Largest free block remains **13,812 B**; zero allocation failures. Boot low watermark stays
  27,176 B. Loop/BLE stack headroom remains 8,380/2,992 B; zero FANET readings are unavailable
  for inactive tasks. These include the 22,496 B active capture allocation.
- This short run shows no declining heap endpoint or sampled fragmentation trend. It is not a
  long soak or proof against every leak.
- Navigate: mean **6.782 ms**, maximum **7.521 ms**, enclosing-slot maximum **8.272 ms**;
  zero normal render-slot crossings in 225 renders. Other flight pages were unvisited.
- The newest IGC contains **130 valid fix records** at one-second intervals, **no gaps**.
  Exact monotonic capture overlap is not encoded. BLE is enabled but connected-client traffic
  is unverified; FANET and optional diagnostic SD logs are disabled.

## Remaining attention

- Button activity remains expensive: 139 excluded button slots peak at 30.762 ms occupied.
  Twenty-nine recovery slots include two crossings and three coalesced ticks; recovery start
  delay peaks at 7.409 ms and first barometer-call delay at 7.472 ms. A retained recovery
  predecessor contains a button event and menu render. Exclusion does not establish safe
  sensor conversion timing or all downstream functional recovery.
- **Slot 78 has an unexplained normal start delay of 7.663 ms and first barometer-call delay of
  7.719 ms.** Its useful work still finishes within that slot's deadline. The bounded recorder
  retained its maxima but not that incident/predecessor, so the cause cannot be attributed to
  GPS, buttons or another service. This is distinct from the fatal ThermalCore readiness bug.
  A future capture should retain the worst start/barometer-delay incident as well as worst
  deadline crossing if this recurs; validate actual pressure conversion readiness on hardware.
- The known ThermalCore startup fatal and thermal-page state lifecycle work are deferred in
  [the miscellaneous firmware bugs backlog](../../../../docs/dev-references/backlog/misc-firmware-bugs.md).
  The repeat did not produce an additional fatal log on the SD card.

Full observer overhead is uncalibrated. All data and button/recovery exclusions are preserved;
this is a reviewed manual diagnostic run, not completed automated release qualification.
