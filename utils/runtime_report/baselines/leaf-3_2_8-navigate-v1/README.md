# Leaf 3.2.8 Navigate development baseline v1

Selected by the user on 2026-10-21 as the reference for future firmware comparisons.
Status: **active provisional development reference**. The report remains `review_required`;
selection is not release qualification or acceptance of the unresolved issues as safe limits.

[Baseline report](report.html) | [Comparison JSON](report.json) | [Raw capture](capture.json)

## Identity and retained evidence

- Hardware: Leaf 3.2.8, 240 MHz, real GPS UART, elapsed device timing, capture schema 3.
- Tested source: commit `a642048` (binary built from `f1f65c8` plus the character-yield change
  before that change was committed). `source-manifest.json` identifies the measured source.
- Exact firmware hash and build/flash evidence are in `firmware-build.json`.
- Original SD file: `/diagnostics/runtime_5.json`, recorded duration **117.461042 seconds**.
- Raw capture SHA256: `82a978dcc5c94d0b2e738796d5b5be2851f9992abc360510defb8cb3d760465c`.
- `manifest.json` pins bundle file hashes. Preserve this version; create v2 for a replacement.
- Exact firmware binary and ELF also have a supplementary local copy under
  `sim/build/runtime-baselines/leaf-3_2_8-navigate-v1/`. That generated directory is ignored by
  Git and is not a durable dependency of the comparison workflow. Reports, raw data, source
  hashes and metadata are versioned; release artifacts need their own durable binary storage.

The original capture and historical reports remain unchanged in
`codex_artifacts/runtime-telemetry/2026-10-21/revision-4/`. The baseline metadata uses stable
profile/scenario identifiers; its original descriptive metadata and report hash are retained.

## Repeat the workload

1. Use this hardware variant and preferably the same unit and installed SD card. Record any
   device, SD, radio-client, power or environmental differences. Match the pinned toolchain
   unless explicitly re-establishing a reference after a toolchain change.
2. Enable Developer mode, obtain a GPS fix, enable Bluetooth, run the flight timer with IGC
   logging, and use Navigate. Keep the captured page/thermal settings equal to `metadata.json`
   and `capture.json`. FANET and optional diagnostic SD logs are off. This profile does not
   exercise web-app traffic, network scanning or other pages.
3. START capture, return to Navigate, hold it for approximately 110 seconds, then STOP and Save
   Timing. Aim for **120 seconds total recorded time**, including menu transitions. The
   current comparison accepts approximately **106-129 seconds**, within 10% of this reference.
   Check the actual exported duration. Do not enter self-test or change workload settings.
4. Record actual BLE client traffic rather than assuming the enabled setting proves it. It was
   unverified in this baseline; matched traffic cannot retrospectively be asserted. Preserve
   IGC/serial/crash evidence and assess button recovery separately.
5. Copy `candidate-metadata-template.json` outside this bundle, fill candidate-specific values,
   and hash the exact installed candidate binary. Keep the stable `profile` and `scenario`
   values only when the workload matches; put run-specific details in `scenario_description`.

From the repository root:

```powershell
python utils/runtime_report/report.py candidate-runtime.json --metadata candidate-metadata.json `
  --firmware-binary .pio/build/leaf_3_2_8_release/firmware.bin `
  --baseline utils/runtime_report/baselines/leaf-3_2_8-navigate-v1/report.json `
  --output candidate-comparison.html
```

Compatibility checks must remain enabled. Schema, clock, footprint, toolchain, captured settings,
profile/scenario or duration changes can require a new reference. Also inspect slot labels and
the schedule: a changed slot's functional workload is not a like-for-like cost comparison.
Never rewrite this baseline or relabel a different workload merely to obtain comparison output.

## Reference measurements and unresolved attention items

| Measurement | Reference |
| --- | ---: |
| Normal observations / deadline crossings | 11,574 / 156 (1.35%) |
| Maximum normal deadline lateness | 0.386 ms |
| Normal accumulated-duration overruns / coalesced ticks | 0 / 0 |
| GPS sentences / sentence crossings | 1,657 / 32 (1.93%) |
| Maximum sentence lateness | 0.354 ms |
| Internal free heap start / end / sampled minimum | 33,516 / 33,548 / 33,216 B |
| Largest free block / allocation failures | 13,812 B / 0 |
| Navigate renders / maximum render | 225 / 7.521 ms |
| IGC valid fixes / maximum interval | 130 / 1 second |

- Slot 78's maximum normal start/barometer-call delays are **7.663/7.719 ms**. Its detailed
  incident was not retained and the cause is unknown. This is not an accepted sensor deadline.
- Button recovery has three coalesced ticks; button slots peak at 30.762 ms occupied. These
  exclusions do not certify safe sensor/GPS recovery.
- The separate ThermalCore startup fatal and requirement to discard all thermal-page runtime
  state on shutdown and flight stop remain open in
  [the bug backlog](../../../../docs/dev-references/backlog/misc-firmware-bugs.md).
- Only Navigate is measured. BLE traffic, FANET, web/scan operation, every power transition,
  hardware UART overflow, full observer overhead and long-term heap behavior are unqualified.

Compare occupancy, lateness, GPS types/stages, pages, memory, stacks, recovery and continuity;
do not judge a candidate solely by fewer red cells or a lower mean. GPS phase and sentence mix
vary naturally, and a single maximum is not a worst-case bound. Establish review tolerances
from repeat runs before adding automated regression thresholds. No PR gate is introduced.
