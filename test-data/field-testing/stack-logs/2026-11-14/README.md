# November 14, 2026 stack and vario timing field test

This collection groups recordings from the same flight by Leaf hardware.
Keep original device filenames inside each unit's directory. Use distinct
unit suffixes if multiple devices share a hardware revision.

| Leaf | Firmware in IGC | Recording | Minimum unused stack |
| --- | --- | --- | --- |
| 3.2.5 | `v0.2.4-865d.tlsbench` | 59 min 53 s, 3,586 fixes, G record matches the bytes on disk. Strict validator rejects a UTF-8 apostrophe in the pilot header. | Loop 7,676 B, Bluetooth 2,996 B, FANET TX 2,076 B, FANET RX 1,248 B. |
| 3.2.6 | `v0.2.4-865d` | 59 min 51 s, 3,579 fixes, valid XLV G-record checksum | Loop 7,680 B, Bluetooth 2,124 B. No FANET workers ran. |
| 3.2.7 | `v0.2.4-b1e` | 60 min 4 s, 3,596 fixes, valid XLV G-record checksum | Loop 7,680 B, FANET TX 2,084 B, FANET RX 1,176 B. No Bluetooth worker ran. |

[Comparison of the three recordings](comparison.md)

## Leaf 3.2.5

- [Original IGC](3.2.5/tracks/2026-11-14-XLV-000-03.igc) spans
  20:46:52–21:46:44 UTC. The original UTF-8 pilot header is preserved.
- [Original stack CSV](3.2.5/diagnostics/stack_3.csv) covers exactly
  60 minutes. Both FANET workers and the Bluetooth worker ran. The pilot
  recalls a phone connection during the flight. The stack CSV establishes
  worker activity, and this session has no connection event log.
- The card's System Events file contained one checkpoint about 39 minutes
  before the timer started. Flight diagnostics are in the IGC.

## Leaf 3.2.6

- [Original IGC](3.2.6/tracks/2026-11-14-XLV-000-01.igc) retains GPS fixes,
  the one-second vario average, IGC flight diagnostics, and its checksum.
  It includes the original track and device metadata.
- [Original stack CSV](3.2.6/diagnostics/stack_1.csv) covers a 60-minute
  monitor session and records each worker's minimum unused stack.
- [System Events excerpt](3.2.6/diagnostics/system_events-flight-boot.csv)
  includes the 818 rows from the boot session containing the flight.
  The firmware CSV header was added. The source rows retain their
  original bytes and order.

The IGC spans 20:46:29–21:46:19 UTC. Use UTC to line up recordings from other
units, since timer start differed from takeoff. Track movement suggests
takeoff about 11 minutes into this recording.

Two ICM FIFO resets occurred at about 21:30 and 21:46 after the recording
timer started. The second coincided with a 750 ms pressure gap and a 780 ms
motion gap. The preceding pressure and motion samples arrived within
20 ms of a System Events checkpoint. The checkpoint's own SD write
duration is unmeasured. A separate 470 ms IGC write near 1:28 was followed
by another FIFO reset. See the
[vario timing robustness backlog](../../../../docs/dev-references/backlog/vario-timing-robustness.md).

Nearby heap samples showed 97,792 B free and a largest free block of
31,732 B. Later reported vario swings had no matching events at the IGC
diagnostic thresholds. The IGC vario field represents the one-second
average. Display and audio have separate timing.

## Leaf 3.2.7

- [Main flight IGC](3.2.7/tracks/2026-11-14-XLV-000-02.igc) spans
  20:46:52–21:46:55 UTC.
- [Preceding IGC](3.2.7/tracks/2026-11-14-XLV-000-01.igc) spans
  20:10:31–20:46:41 UTC. It was started automatically in the same boot and
  ends 11 seconds before the first fix of the main recording.
- [Original stack CSV](3.2.7/diagnostics/stack_1.csv) covers a 60-minute
  monitor session. The SD card's diagnostics folder contained the stack
  CSV only. Flight diagnostics are in the two IGCs.

[Source manifest and SHA-256 hashes](source-manifest.json)
