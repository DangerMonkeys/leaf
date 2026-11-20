---
title: 3.2.5 FANET and Bluetooth shutdown tests, November 20, 2026
description: Live peer reception, phone-connected shutdown, menu and Web App lifecycles, saved stack headroom, and IGC header validation results.
---

# 3.2.5 FANET and Bluetooth shutdown tests

The committed shutdown candidate passed two normal shutdowns on a Leaf 3.2.5 with
Bluetooth connected to the user's phone and FANET enabled with nearby transmitting
units. Both recordings finalized, both Stack captures saved, and the unit returned
to charging. Menu Wi-Fi and Web App pause/resume also completed during recording.
No firmware changes were made for this test.

**IGC format validation remains blocked by the known UTF-8 pilot-name header issue.**
Both raw G-record checksums match, but the strict validator rejects the curly apostrophe.
The separate `codex/igc-header-text` fix (`00275016`) is not in this candidate.

## Exact candidate and configuration

- Source: `8a435a426f5383b0097ee96df40cad47ed4ea873`, `codex/usb-bench-console`.
- Environment: `leaf_3_2_5_bench`, stock TLS, USB console enabled.
- Reported firmware: `0.2.4-8a4.bench+h3.2.5`.
- Binary SHA-256: `42edeeffa28e30cd9d67c0003d00258575467e57154c901d2598354581fd3ea6`.
- Linked static RAM: 86,388 B; linked flash: 2,437,852 B. These are variant totals,
  not another saving or a matched comparison with the 3.2.8.
- Device MAC: `F0:F5:BD:56:16:A0`; Bluetooth name `Leaf16A0`; FANET identity `FB16A0`.
- The detected FANET radio used the US region. Bluetooth and Developer mode were on.
  System/network/web/CPU diagnostic flags were off. The original startup page was Navigate.
- The user confirmed phone connection before the first shutdown, reconnection after wake,
  and reconnection after closing the Web App. Peer IDs observed: `FBD364` and `FB5F38`.
- Pressure, IMU and GPS were physical inputs. No UDP sensor injection was used.

Both OTA slots flashed and verified. The formatter and build passed. The SD card was
identified as FAT32 volume `DC408B34`, 8,047,788,032 B total, before copying files.

## Hardware results

| Workload | Result |
| --- | --- |
| Initial flight with phone connected and FANET running | GPS fix, usable climb-rate status, BLE and both FANET workers observed |
| Menu Wi-Fi load/unload during recording | Repeated entry/exit completed with flight timer and FANET running |
| First normal power-off during recording | Completed logbook entry, IGC `CAUSE=POWER_OFF`, shutdown-saved Stack CSV, radio workers released, charging reached |
| Wake and phone reconnect | Normal wake succeeded; the user confirmed reconnection; FANET reinitialized on the next recording |
| Open Web App during the second recording | Flight timer and climb-ready status stayed active; BLE/FANET stopped as designed; app/status/session/profile/navigation reads returned HTTP 200 |
| Close Web App | FANET restarted, BLE advertised again, and the user reconnected the phone; recording remained active |
| Second normal power-off with phone reconnected | Charging reached 5.125 s after the synthetic button-hold command; both logs saved, radio workers released |
| Charging upload cancellation and USB handoff | The normal two-tap cancellation released the card without powering on or deleting pending recordings |

The 5.125 s observation includes approximately 1.8 s of normal button hold recognition;
it is not a direct measurement from the shutdown request. The existing 8 s deadline
starts at that request. No watchdog recovery, message-bus lock fatal or reboot was
observed in either shutdown. Increasing device uptime in the saved records and retained
boot heap minima are consistent with the same boot across both cycles.

The first flight lasted **285 seconds**; the second lasted **489 seconds**. Stack monitoring
covered 989.592 s and 495.802 s respectively, including preparation/menu activity.
The two FANET CSVs contain **17 and 108 observations**, from both nearby peers, with
**zero logger queue drops**. USB traces also contain PFLAA output from the BLE forwarding
path. Phone connection was user-confirmed; per-notification delivery acknowledgements
were not captured.

## Stack and heap observations

| Task | Allocated stack | Lowest unused across these captures |
| --- | ---: | ---: |
| Main loop | 12,288 B | 7,836 B |
| Bluetooth worker | 5,120 B | 2,996 B |
| FANET TX | 4,096 B | 2,076 B |
| FANET RX | 4,096 B | **1,212 B** |

The second CSV retained minima across **two instances each of BLE, TX and RX**, verifying
that Web App stop/restart did not discard earlier readings. The November 14 field captures reached an even lower RX minimum of 1,176 B; the
1,212 B measured here is not the branch-wide minimum. Keep its 4 KiB allocation;
this short workload does not justify reducing it. No task stack allocations were changed.

Representative free internal heap was 92,228 B in the initial active configuration,
56–58 KiB while menu Wi-Fi was active, and about 89 KiB after closing the menu. After
Web App closure, free heap was 88,048 B and the largest block was 16,372 B. The boot-wide
minimum fell to **20,208 B**, first observed after wake; the exact transient responsible
was not isolated. Final charging free heap after upload cancellation was **180,448 B**,
with a 31,732 B largest block. These are mixed lifecycle readings, not a worst-case reserve
or a paired baseline comparison. No allocation error was observed in the tested paths.

The charging uploader's retained stack reading was 8,564 B unused. That worker was not
part of the on-state flight stack CSV and is not an on-state heap allocation saving.

## File integrity and timing limits

Both logbook JSON entries are complete and refer to the correct saved IGCs. Both IGCs
have B records and final `CAUSE=POWER_OFF` markers. Recomputing the logger's MD5 over
the original record bytes plus LF exactly matches each G record.

The strict `vali-xlv.exe` checker returns `STATUS_IGC_FAILED: non-printable record` for
both files because line 4 contains the UTF-8 curly apostrophe in the pilot name. Raw
checksum agreement is **not** a strict format-validation pass. The preserved files were
not edited or re-signed. The existing separate header-text fix needs integration before
claiming these real-profile recordings satisfy that validator.

The first IGC contains one slow-write diagnostic (**136 ms**) and one FIFO diagnostic
(`N=1`, `R=2`). It contains no reported motion/pressure gap over the diagnostic threshold.
The second IGC has no FD events. B-record timestamp gaps reach 2 s in the first file and
20/50 s in the second. GPS fix was unavailable for parts of the second recording and
later returned; B records require both usable GPS and a ready barometer. These observations
do not establish the cause of each gap. No Runtime capture or dynamic climb/audio stimulus
was used, so this is **not a vario timing or beeper performance qualification**. Preserve
the [vario timing backlog](backlog/vario-timing-robustness.md) and these files for follow-up.

## Test corrections and final state

An initial developer-menu navigation selected the adjacent forwarding toggle. It was
toggled back on the next control action, and the FANET CSV headers confirm forwarding was restored to
1. Reception logging was temporarily enabled and restored to its original off state.
One read-only web probe used the nonexistent `/api/debug/memory` URL and returned 404;
that was a harness URL error, not a product failure. The actual page is `/app/debug/memory`.
A wake capture lost its COM handle during the normal USB ownership handoff; reopening
succeeded, with no reset observed.

The charging upload cycle still owned the card when readback was attempted, so the
normal two-click cancel path was used. Cancellation and SD release passed; completed
Leaf Log delivery was not verified in this session. Existing pending-upload state was
left to the firmware's normal handling.

The unit is left **OffUSB / Charging**, with the test candidate installed, original
configuration flags restored, and **7,944,634,368 B free** on its card. The new recordings
remain on the card. Profile, route and waypoint bytes match between the two readbacks;
no user recordings were deleted or profile names changed.

Evidence is in `test-data/field-testing/shutdown/2026-11-20-3.2.5-fanet/`: exact candidate
identity, build/flash logs, USB/HTTP traces, original SD copies, analysis, and reproducible
verification. These tests close the previously missing live FANET plus connected-phone
shutdown coverage. They are bounded bench tests, not release qualification or a guarantee
against every possible callback/shutdown interleaving.
