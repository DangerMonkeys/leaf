---
title: Firmware heap savings log
description: Leaf memory audit changes, measured savings, and on-device verification.
---

## Batch 1 · Leaf 3.2.5

One bench build covered web request and route lifetimes, diagnostic heap history,
the file picker, obsolete GPS storage, and launch-detector layout. Linked static
RAM fell by **7,600 B**, from 107,724 to 100,124 B. Free internal heap rose by
**7,480 B at idle** and **40,060 B after closing the Web App** following the
same two screenshots and 8,000-byte request. The largest free block after
close rose from **2,292 to 31,732 B**.

### Change log

| Audit item | Change | Memory result on 3.2.5 |
| --- | --- | --- |
| N02a | Release the XBM screenshot buffer after the synchronous response. | **11,680 B less retained** across the two-screenshot step. The baseline lost 12,128 B after the screenshots and the candidate lost 448 B. The buffer still exists during capture and send. Its static owner shrank by 12 B. |
| N03a | Register the 13 canonical commissioning routes on the factory connection while commissioning is incomplete. Keep status and completion retries available on that connection after completion. Remove the 13 duplicate debug aliases. | Audit estimate of **3,280 B** of route payload absent from ordinary Web App sessions. The device returned HTTP 404 for both MAC-address paths during normal hosting. The measured web-open gain also includes the static reductions and allocator effects. |
| N03b | Free the installed Web App and developer handlers when hosting stops. Allocate them again on reopen. | The before/after gain widened by **8,196 B** at the close transition. This includes handler release and other heap changes during Wi-Fi and BLE transitions. Handler bytes were not isolated. Reopening and the Device Maintenance status endpoint passed. |
| N04 | Release the WebServer request-argument arrays after synchronous processing and at shutdown. | **8,032 B less retained** across the 8,000-byte route-request step. The baseline lost 8,256 B and the candidate lost 224 B. |
| N05a | Borrow the existing parsed request body by const reference for synchronous handlers. | One String copy avoided per handler. Estimated **8,016 B** for the 8,000-byte request or **18,016 B** at the editor limit. These estimates apply while the handler runs. The test did not isolate the handler peak from the HTTP parser's own allocations. The empty fallback costs 24 B of static storage. |
| SYS-01-a | Allocate the 32-entry heap history when System Events diagnostics are enabled. | **3,063 B less linked static storage** for the 3,072-byte buffer after accounting for the pointer, enable flag, and mutex handle. |
| SYS-01-b | Discard the heap history on disable and start with an empty buffer on re-enable. | **2,864 B returned** on each of two live disable cycles. The live values include other heap changes around the HTTP requests. |
| UI-01 | Allocate the bounded filename table while the picker is open and free it on final dismissal. | **3,096 B less linked static RAM** for the Nav Data menu owner. Opening consumed 3,120 B. After visiting both folders, dismissal returned 2,976 B and restored the largest free block from 28,660 to 31,732 B. |
| I01 | Remove 21 unused TinyGPSCustom fields and their obsolete GSV/GST scaffolding. The live GSV parser and fix fields remain. | **1,092 B** from the removed members. The linked GPS object shrank by **1,200 B** with I02. |
| I02 | Remove the unused GPS NMEA buffer and index. | **108 B** from the removed members, included in the measured GPS object delta above. |
| S01 | Reorder the launch sample fields at full precision. The 32-sample capacity remains. | **256 B less linked static RAM**. The detector shrank from 1,592 to 1,336 B. |

### Bench measurements

Each checkpoint reports the median of five USB console reads after a two-second
settling period. Both runs began on User with the vario ready, GPS without a fix,
flight timer stopped, and firmware SD access available. Bluetooth was configured
on, FANET region was 1, and Developer mode was on with diagnostic logs disabled.
The host opened the Web App over the saved Wi-Fi connection. The 8,000-byte JSON
request intentionally omitted route points and verified the expected HTTP 400
validation response without saving a file.

| Checkpoint | Baseline free B | Candidate free B | Free B gained | Baseline largest B | Candidate largest B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Idle on User | 40,292 | 47,772 | 7,480 | 31,732 | 31,732 |
| Web App open | 79,496 | 91,648 | 12,152 | 24,564 | 31,732 |
| After two screenshots | 67,368 | 91,200 | 23,832 | 24,564 | 31,732 |
| After 8,000-byte route request | 59,112 | 90,976 | 31,864 | 24,564 | 31,732 |
| After Web App close, on Navigate | 4,700 | 44,760 | 40,060 | 2,292 | 31,732 |

At the end of the matched workload, the recorded boot-to-date free-heap minimum
was 3,220 B on the baseline and 38,000 B on the candidate. The loop task reported
7,816 and 7,780 B of unused stack, respectively. The checkpoint and static RAM
figures describe the same build and should not be added together.

An 18,000-byte request run after the candidate's picker and diagnostic
lifecycle checks ended with a USB disconnect. The device reappeared on the
charging screen. After waking the unit, the candidate returned the expected
validation response at that size. It had 91,208 B free after the request and
45,164 B after closing. Its boot-to-date minimum reached 21,700 B during that
run. An 18,000-byte attempt on the baseline also timed out after the
screenshots. The large-request result is sensitive to prior activity. The
remaining HTTP parser allocations and the cause of the disconnect warrant
investigation before using this stress case as a prerelease pass criterion.

### Verification and provenance

- Both app slots on the connected Leaf were flashed and hash-verified.
- The hardware test exercised USB wake and navigation, repeated Web App
  open/close, user status, waypoint listing, screenshots, route request
  validation, route-optimizer diagnostics, Device Maintenance status, the
  picker lifetime, and System Events enable/disable. The diagnostic setting
  was restored. Factory-network commissioning and completion retry still
  need a fresh commissioning fixture.
- Simulator checks passed for flight-start windows, GPS UART handling,
  USB serial, and developer logging. Firmware configuration tests passed.
- Baseline image: `0.2.4-03ed.bench+h3.2.5`, SHA-256
  `46705b76c2b2e4d1557c24aec42a2eac5777c20422ebba4298573d45ce5c3b6f`.
- Candidate image: `0.2.4-dd5d.bench+h3.2.5`, built with the batch firmware
  edits on `dd54b9edf2c38ae30436d991f62575f9faa9e81e`. SHA-256
  `6b96404cbb42ffea128be215a421959a0ed866b6b18f8c6eb655c368717571c5`.
- Raw USB captures and the ELF summary are in `utils/bench_baselines/batch-01/`.
  The tested candidate BIN, ELF, map, and flash log are archived locally in
  `.pio/bench-batch-01/final/`. [USB bench console guide](usb-bench-console.md)

### Route and waypoint verification

A follow-up on the same candidate saved and activated a two-point route
through the Web App. The navigator showed it as the active route. Reading
the SD card directly confirmed the full route name, both waypoints and
their cylinder radii, and the goal role on the final point. The navigator
applied its existing 15-character route label limit.

The Web App listed and activated a separate GPX file. The navigator reported
both expected waypoints with their coordinates and elevations, and the SD
card's navigation pointer referenced the GPX. The original pointer was restored
byte for byte and both fixtures were removed. After a USB restart, the
original navigation fields matched and the Leaf was returned to the User
screen with the Web App closed.
Evidence is saved in `utils/bench_baselines/batch-01/route-and-waypoint-verification.json`.

## Batch 2 · Route request memory

| Audit item | Change | Memory accounting |
| --- | --- | --- |
| N06a | Stage the route editor and import JSON bodies in a temporary SD file through the Arduino core's 1,436-byte receive buffer. Enforce the 18,000- and 10,000-byte limits before writing to SD. | The raw JSON path avoids the old full receive buffer and its full String copy. These previously overlapped during receipt. The import handler still holds the decoded XCTSK data while parsing it. The form fallback keeps the existing multipart and URL-encoded `plain` behavior. |
| N07a | Normalize the editor's parsed document in place. Verify that the complete serialized route reached SD before replacing an existing route. | The editor no longer holds a second full route document. The saving depends on the input fields and point count. |

The candidate uses 100,204 B of linked static RAM, 80 B more than Batch 1.
That includes the request state and the bench-only phase measurements.
Use `routeheap` over USB to read free internal heap before receipt, the
receipt minimum, free heap before editor normalization, and the normalization
minimum. Those are local phase minima. Measure them separately from the
boot-to-date heap low water mark.

| Workload | Batch 1 | Batch 2 |
| --- | --- | --- |
| 18,000-byte editor request, 20 points | Timed out and USB disconnected | HTTP 200 in 297 ms |
| 10,000-byte import request, 20 points | HTTP 200 in 218 ms | HTTP 200 in 235 ms |

The editor used **5,408 B** from the first raw callback through SD receipt and
**1,028 B** during in-place normalization. Import receipt used **2,788 B**.
Receipt includes SD and network allocations after the raw callback starts.
The pinned core allocates its 1,436-byte receive buffer before that callback.
Batch 1 has no phase instrumentation. Its old receipt path overlapped an
18,001-byte or 10,001-byte receive payload with a second full body String.
The net before/after peak delta for either phase was not isolated.

The editor rejected 18,001 bytes, malformed JSON, 21 points, and an invalid
latitude. Import rejected 10,001 bytes. A request that stopped after 3,000 of
18,000 bytes returned HTTP 400 and the next Web App status call returned 200.
Reading the SD card confirmed that existing test routes retained their
coordinates, elevations, goal, and radii, and that the temporary request file
was gone. The pre-existing active navigation pointer matched its original
hash. Test route files were removed. Route guidance and route optimizer
simulator checks passed.

The route-only image was `0.2.4-b25d.bench+h3.2.5`, built with the route
changes on `b2506c25`. Both app slots were hash-verified. Image SHA-256
`7004bcc2a6059140b202ab39e5b9cf35342bcf0e3b7fa568cb209f2093b4d24d`.
The measurements and SD checks are in `utils/bench_baselines/batch-02/`.

## Batch 3 · Idle Wi-Fi shutdown

**N20** powers down the Wi-Fi driver when the user exits the menu or leaves a
standalone Web App access point. It also stops Wi-Fi after an unsuccessful
commissioning search and on existing Leaf Log disconnect paths. Wi-Fi setup
keeps the station connection when handing off a successful join. The factory
connection remains available for completion retries. Saved-network reconnect
still starts when the menu needs it.

The table compares the route-only build with the Wi-Fi shutdown build on the
same Leaf 3.2.5. Each checkpoint is the median of five USB reads after a
two-second settle.

| Checkpoint | Route-only free B | Wi-Fi shutdown free B | Free B gained |
| --- | ---: | ---: | ---: |
| Idle before opening the menu | 46,144 | 97,480 | 51,336 |
| After closing the first station Web App session | 45,760 | 79,092 | 33,332 |
| After using Leaf AP, reconnecting, and exiting the menu | 40,516 | 73,548 | 33,032 |

The active Web App consumed comparable amounts of memory. It served the
status endpoint on the saved network in both cycles. The candidate reported
`WIFI_OFF` at all three idle checkpoints, `WIFI_AP` on Leaf's access point,
and `WIFI_STA` after the saved network returned. The first and second
Web App opens took 1.64 and 1.69 seconds, measured from the User screen.
After closing the access point, the saved network returned in 0.77 seconds
versus 0.53 seconds in the route-only run. The access-point test verified its
mode and IP from USB. It did not move the PC onto the Leaf AP.

The Wi-Fi candidate used 100,204 B of linked static RAM, matching the
instrumented route build. Hardware coverage includes USB charging/wake,
normal Web App reconnect, both hosting modes, and returning to idle.
A fresh factory commissioning session, a real Leaf Log transfer, and OTA
were not exercised on this unit.

The final image was `0.2.4-064d.bench+h3.2.5`, built with N20 and the
legacy form fallback on `06453e64`. Its SHA-256 was
`ddd68959de69f7783c1836ed694531aacde1ddae8b25691f0f642d81066a91ec`.
Both app slots were hash-verified. The final image also passed the full route
regression and decoded a URL-encoded `plain` field through the legacy parser.
Leaf Log sync and charging dialog simulator checks passed. The paired reports
and the final route regression are in `utils/bench_baselines/batch-03/`.
