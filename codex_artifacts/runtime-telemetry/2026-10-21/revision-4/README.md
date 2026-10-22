# UART yielding between characters

Telemetry and earlier baseline reports were committed first as `f1f65c8`. This build adds an
atomic tick comparison before each UART character read, retaining partial sentences across
slots. Scheduled tasks run before UART reading resumes in the next slot. Sentence parsing
and completion callbacks stay inline, as requested; they can still cross the boundary.
There are no new capture records, timestamps, locks or allocations per character.

`firmware-build.json` identifies the exact Leaf 3.2.8 image installed to both OTA app slots.
Its firmware/source hashes identify the tested version, built from the first telemetry commit
plus the character-yield change. Static RAM stays at 103,564 B; capture allocation stays at
22,496 B. The bounded yield regression covers partial lines, final payload characters,
terminators and expiry during callbacks with capture both off and on. Telemetry accounting,
lifecycle, integration and UDP regressions also pass.

[Post-change device report](device-uart-selftest.html) now captures the operator's approximately
90-second Navigate run followed by approximately 30 seconds of accidental manual self-test.
The actual capture lasts 116.391 seconds. It is explicitly marked as a mixed workload and
comparison to revision 3 is withheld. See [reviewed findings](device-uart-selftest-findings.md).
Raw evidence, metadata, the self-test log and a position-free IGC summary are preserved here.
No firmware change or reflash was performed while retrieving this capture.

## History

Git attributes the change from one-character reads to draining available data to **Benjamin
Pelletier**, July 18, 2025, commit `78c514a09d17bc40096679508aecf325bdc51ea1`,
[Split GPS hardware abstraction from GPS instrument (#162)](https://github.com/DangerMonkeys/leaf/pull/162).
The earlier `readBufferOnce()` consumed one character per scheduler timer check. The new
`readData()` loop processed available lines without an internal slot-expiry check. This was
already present in v0.1.0.

**Benjamin Pelletier** later introduced the current per-sentence scheduler yield on August 12,
2025, commit `927e00d90d1700784cebb10826b234b45c9b4d66`,
[Transmit GPS text via message bus (#175)](https://github.com/DangerMonkeys/leaf/pull/175).
Consequently, the immediate pre-change firmware yielded between lines; it did not invariably
drain the entire UART buffer before checking the timer. Neither implementation checked the
timer between characters inside a line. These attributions come from local Git author records;
they do not establish who reviewed or merged each PR.

## Startup fatal-error diagnosis

[Decoded diagnosis and simulator reproduction](startup-fatal-findings.md) identify an unguarded
ThermalCore altitude read while the barometer restarts after USB-charging wake. The SD log was
retrieved and verified; installed firmware was left unchanged for the pending timing repeat.

## Latest repeat without self-test

[Repeat device report](device-uart-clean.html) and [reviewed findings](device-uart-clean-findings.md)
preserve `/diagnostics/runtime_5.json`, a 117.461-second recorded interval. Sentence crossings
are 32/1,657 (1.93%) with maximum 0.354 ms lateness; normal slots have zero duration overruns
and zero coalesced ticks. Heap endpoint is +32 B, no allocation failures; latest IGC records
130 valid one-second fixes with no gaps. Button recovery and a separate unexplained 7.719 ms
normal barometer-call delay remain attention items. The longer run is not a formally comparable
baseline against revision 3. Previous mixed self-test evidence remains intact.

## Selected development baseline

The latest repeat was selected by the user as the provisional Navigate reference. Its immutable
versioned bundle is `utils/runtime_report/baselines/leaf-3_2_8-navigate-v1/`; the active registry
and runtime-assessment guide point future comparisons there. Raw evidence is unchanged. Known
issues and incomplete coverage remain recorded; selection does not establish release fitness.
