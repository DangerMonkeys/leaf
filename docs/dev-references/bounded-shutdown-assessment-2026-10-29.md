# Shutdown candidate assessment — 2026-10-29

**Ready for physical shutdown testing, not yet approved for production.**

Candidate firmware source: `cc457326611ef350646b011e556f76949216a363` on
`codex/bounded-shutdown`, branched from main `07140964`. This assessment commit
only records results; the firmware remains the exact candidate above. Route
optimization is not included. Its latest tested source remains `2882f408` in the
separate `codex/route-solver-assessment` worktree.

Implementation, limitations, fault selector, and the hardware capture checklist:
[Bounded shutdown](bounded-shutdown.md).

## Completed verification

- Repository formatter and `git diff --check` passed.
- Both PlatformIO environments built successfully with a clean candidate tree:
  `leaf_3_2_6_release` and `leaf_3_2_6_shutdown`.
- The host simulator build and `test-shutdown`, `test-shutdown-menu`,
  `test-developer-logging`, `test-runtime-capture`, and `test-runtime-lifecycle`
  completed successfully. Tests use fresh, exclusively created fixture folders
  so a previous run cannot supply an old completed flight or recovery event.
- The shutdown policy test covers unsigned timer wrap, button release gating,
  and concurrent producer admission/draining. The integration test runs twelve
  scenarios: normal, injection disabled, lock/save/worker/sound/peripheral/BLE
  stalls, blocked main loop, nested cleanup, modeled RTC backstop, and held-button
  recovery. Normal shutdown finalizes a flight; faults after the essential-save
  stage retain a completed flight. Recovery overrides a boot-to-on request and
  enabled Bluetooth, and records the previous stage/reason in system events.
- Six injected ordinary stalls released the modeled latch at **8,004 ms**. The
  independently modeled RTC-backstop case acted at **10,004 ms**. The 4 ms offset
  is timer phase in this scenario; physical timing has not been measured.
- The developer-only menu test covers selection cycling, wrap, cursor order,
  LEFT-to-Back behavior, and screenshots with/without a detected FANET radio.
  Both screenshots were visually reviewed.
- Both native ELFs place `tick()`, `trip()`, `hardwareTrip()`, and
  `esp_timer_get_time()` in `.iram0.text`; the ISR's relocation dependencies use
  internal/RTC state and registers, with no locking atomic helpers or flash
  strings. The native worker parking acknowledgement is a direct byte store
  followed by suspension, outside driver locks.

| Build | Version | BIN bytes | Static RAM reported by PlatformIO |
| --- | --- | ---: | ---: |
| Normal | `0.2.4-cc4+h3.2.6` | 2,416,656 | 107,404 B |
| Fault test | `0.2.4-cc4.shutdown+h3.2.6` | 2,417,152 | 107,412 B |

These static sizes are not additional heap costs. There is one new hardware timer,
small fixed guard state, and SDK timer allocation, with no new FreeRTOS task or
task-manager slot. On-device CPU/heap overhead is still to be assessed.

## Deployment

The fault-test image was flashed to the attached **3.2.6**, USB identity
`F0F5BD5467A0`, in both app slots (`0x10000`, `0x340000`). Esptool verified both
writes and exited 0. The first app-mode attempt caused the expected COM14-to-COM13
handover; the one allowed bootloader retry succeeded. After reset, the device
returned on COM14 with application USB PID `0002`, and SD storage was accessible.
Both `airport_road.json` and `apartment-compute-8.json` retained their pre-flash
SHA-256 checksums. No fault was automatically armed or exercised on hardware.

Fault-test BIN SHA-256:
`756215cdff0c5c59194dcb5b54ca582a4bbec84a7f1bf2aeaad58990a5c61bba`

Normal BIN SHA-256:
`4f79aa1d8624d352dc0f854e6e5af250b4f37d04218e6373b8522d9002c1dcbf`

Local raw evidence is retained under
`sim/build/hardware-evidence/shutdown-cc457326-20261029-180740/`: both BIN/ELF
pairs, build identifiers, simulator/build logs, IRAM symbol/relocation audits,
flash logs and deployment manifest, and the post-flash USB/route check. This
ignored directory is not part of the Git commit. The previous nav BIN/ELF and
field/runtime evidence remain preserved in the nav worktree for rollback.

## Still required on hardware

Start with **Off fault = OFF**: save a short flight and shut down with Bluetooth
off, advertising, and connected to a client, on both USB and battery. Then follow
the linked fault checklist. Capture actual power-latch/visible reset timing,
flight/IGC completion, system events, boot diagnostics, and any watchdog/core dump.
A representative long flight and slow/nearly full SD card must fit the save
budget before production acceptance.

Desktop tests do not establish that the original Goodbye freeze was reproduced
or identify its cause. They also do not validate physical GPIO power removal,
RTC reset timing, real NimBLE worker quiescence, radio behavior, or storage-driver
failure handling. Those remain explicit hardware review items.
