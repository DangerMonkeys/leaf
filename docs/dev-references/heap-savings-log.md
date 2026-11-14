---
title: Firmware heap savings log
description: Leaf memory audit changes, measured savings, and on-device verification.
---

[Branch-wide heap savings report](heap-savings-summary.md)

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

## Remaining hardware tests for batches 1–3

Record the firmware commit and image SHA-256 for each run. Keep the results
next to the corresponding bench captures. Use the release image for checks
that depend on normal USB, charging, or OTA behavior. [Firmware release checklist](releasing.md)

| Test still open | Setup and expected result |
| --- | --- |
| Factory commissioning and completion retry (N03a, N03b, N20) | Use a spare Leaf with erased NVS and a test SD card on the factory network. Run the factory tool through discovery, reset, self-test details, restart/reconnect, and completion via the canonical endpoints. Drop the first completion reply and confirm that status and a repeated completion request succeed on the same factory connection, including after closing a Web App session on that connection. Confirm the next boot skips factory discovery after completion. Verify the Device Maintenance repair action on an eligible fixture and confirm ordinary Web App hosting leaves the factory endpoints unavailable. |
| Live Leaf Log retry and cancellation (N20) | Batch 9 verified saved Wi-Fi reconnect, actual uploads, persisted delivery records, and return to USB mass storage. Interrupt the network during a transfer, verify that the flight stays pending, then confirm a retry reconnects and completes without a duplicate. Check that cancellation or power-on hands off the SD card only after the upload worker exits. Simulator coverage is recorded above. |
| OTA over saved Wi-Fi (N20) | Update a spare unit over its saved network with a known test image. Verify the update completes, the expected version boots, settings and SD files survive, and the Web App reconnects after the reboot. The bench USB flashes exercised a different update path. |
| Leaf access point and Wi-Fi setup (N20) | Join Leaf's access point from the host, load the Web App, and exercise a route request. Test a successful join through the setup portal and a cancelled setup. Verify the station connection survives a successful handoff, and check saved-network reconnect and idle radio shutdown on exit. The bench run verified AP mode and IP over USB and exercised HTTP on the saved station network. |
| Repeated route stress and SD write failure (N06a, N07a) | On the combined firmware, repeat the 18,000-byte editor request after screenshots and other Web App activity, then reopen the app and repeat. Record any disconnect and the phase heap readings. With a disposable full or failing SD card, verify that a staging or write failure preserves an existing route and a later retry can save successfully. The combined candidate passed the maximum-size, oversize, and interrupted-transfer checks on the bench card. |

## Batch 4 · Runtime capture and profile picker

| Audit item | Change | Measured or linked memory |
| --- | --- | --- |
| SYS-05-a | Add `discard` to the Developer runtime API. A running capture must be frozen before it can be discarded. | Free internal heap rose **22,288 B** after discarding a frozen capture. |
| SYS-05-b | Turning off Developer mode releases a running or frozen capture. Re-enabling Developer mode leaves capture idle. | Free internal heap rose **22,376 B** when Developer mode was turned off with a capture running. Both actions release the same **22,496 B** capture payload. |
| UI-02-B | Allocate the profile picker model when its page opens and release it when that page closes. | The linked `profileSelectPage` object shrank from **1,868 B to 48 B**, releasing **1,820 B** of static RAM while closed. The open picker held about 1,260 B of internal heap with the test fixture. Closing returned to the same heap reading on the first cycle and within 140 B on the second. |
| UI-03-A | Keep IDs and resolved display labels in the open picker. | Sixteen glider rows hold two Strings each instead of five, a **768 B** reduction in String headers inside the open model. This overlaps with UI-02-B while the picker is closed. String payload savings vary by profile. |

The 3.2.5 test selected a second pilot and a second glider using the
physical menu path driven over USB, reopened the picker, and verified both
saved IDs through the Web App. The fixture included a legacy glider
`name` field and a display-name override. The original SD profile file was
restored byte for byte. Runtime checks verified HTTP 409 when attempting
to discard an active capture, successful freeze and discard, successful
export and JSON download, and no restart after cycling Developer mode.
Simulator recorder, lifecycle, and scheduler integration checks passed,
including freeing an already frozen capture on Developer mode exit.

The `0.2.4-a40d.bench+h3.2.5` image was built from local changes on
`a4075ce4`. Both app slots were hash-verified. The build used 98,380 B of
static RAM versus 100,204 B in Batch 3, a total reduction of 1,824 B.
Image SHA-256
`6dd480e9a7f83db3dfb8b9520223a2ace945216d2483b22ed24809249f555acf`.
Bench evidence is in `utils/bench_baselines/batch-04/`.

## Batch 5 · FANET lifecycle

| Audit item | Change | Memory accounting |
| --- | --- | --- |
| N15a | Release the FANET protocol, radio driver, task stacks, and mutex while the Web App or Wi-Fi setup is open. Restart when the active flight needs the radio again. Block fresh FANET allocations while the app is open. | The saved-network Web App returned HTTP 200 with both FANET workers absent during a stationary bench flight. The radio and both workers returned on close. The matched ground start/stop cycles below measure the resource cost without Wi-Fi and Bluetooth handoffs in the comparison. |
| N16 | Preserve boot-time hardware detection and sleep the radio after the probe. Allocate runtime resources only when a flight or ground-tracking mode needs the radio. | The unit detected its FANET radio at boot and reported zero FANET workers. This avoids the same allocations measured on stop in N15a. |

Free internal heap before the first menu visit rose from **99,304 B** in
Batch 4 to **111,356 B** in Batch 5 on the same 3.2.5 with the same radio
configuration. The largest free block grew from **36,852 B to 49,140 B**.
Each value is the median of five USB readings.

| Ground-tracking cycle | Active free B | Stopped free B | Free B returned |
| --- | ---: | ---: | ---: |
| 1 | 48,980 | 60,504 | 11,524 |
| 2 | 48,836 | 60,512 | 11,676 |
| 3 | 48,836 | 60,504 | 11,668 |

The final build uses 98,308 B of linked static RAM, 72 B less than Batch 4.
The FANET singleton shrank by 80 B, from 1,940 B to 1,860 B.
The radio heap reclaimed by teardown and the allocation avoided at boot
describe the same resources.

The driver, SPI module, and HAL share the same allocation lifetime. Teardown
withdraws the interrupt target and waits for the RX and TX workers to park
before deleting them. A timeout retains the resources for a later stop.
The tests used the existing saved US region and a stationary unit with
no GPS fix.

The final image was `0.2.4-a40d.bench+h3.2.5`, built with both batches on
`a4075ce4`. Both OTA app slots were hash-verified. Image SHA-256
`42fbd1981c04af150ccfa941c70faac28d9195d3c5e6dbf4e5292e9a424b05b9`.
Bench evidence is in `utils/bench_baselines/batch-05/`.


### Remaining FANET hardware coverage

Exercise RX and TX against a second FANET radio with a real GPS fix.
Verify position and neighbor updates before and after a Web App pause.
Repeat while traffic is active to check the interrupt and worker
handoff under load, and measure the time until the first valid packet
after resuming. Recheck the Wi-Fi setup portal on a spare unit, including
a cancelled setup and a successful network join. The bench run covered
the saved-network Web App path.


## Batch 6 · Route import and saved waypoints

| Audit item | Change | Memory accounting |
| --- | --- | --- |
| I30a | Parse the original XCTSK String after skipping its optional prefix. | Removes one payload-sized String copy. The 8,192-byte valid fixture exercises the upper limit. Candidate route-import phase used 12,184 B from normalization through the response. The baseline firmware has no matching phase probe. |
| N07b | Validate the saved-waypoint file one record at a time, then stream the Web App view in file order. | Candidate listing used 16,936 B from file open through the response for the 21,197-byte fixture. This includes SD and network memory. The previous run's boot-to-date low moved from 55,408 B to 53,088 B on listing and the candidate's stayed at 57,144 B. These are cumulative minima and do not isolate the listing's before/after peak. |
| I30c | Find whether saved waypoints exist during the validation pass. | The reader carries one JSON value at a time. A separate phase delta for the picker existence check was not measured. |
| I30d | Reuse that reader when merging or loading the saved-waypoint source. | Eliminates the full-file JSON tree during navigator load and merge. The underlying stop-and-read workload shared the same 21,197-byte file. No separate on-device peak delta was isolated. |
| I31a | Copy the path and active indices out of the persisted navigation-state document before loading its target. | The small state document no longer overlaps with source parsing. The exact heap delta depends on state file size and was not isolated. |

The USB console's `navheap` reports free heap at the start and local minimum
for route import, then the same two values for a waypoint listing. Candidate
linked static RAM was 98,332 B, 24 B more than Batch 5 due to bench
instrumentation. Idle free heap before opening the app was 111,332 B.

Both firmware images listed 113 valid records, returned the same index for
duplicate coordinates, skipped the invalid coordinates, activated a point,
and supported rename and delete. The candidate preserved unrelated waypoint
metadata, rejected compressed XCTSK and malformed or oversized waypoint
files, and restored both the staged user-waypoint source and an imported
20-point route after a reset. Route guidance and route optimizer simulator
checks passed. Writes still use the existing document path to preserve
metadata. The near-limit listing took about 0.6 s on the candidate and
0.4 s on the baseline in these runs. Rename took 1.45 s versus 0.69 s,
including its subsequent navigator reload.

The baseline was Batch 5's `42fbd198…` image. The candidate was
`0.2.4-068d.bench+h3.2.5` built with local changes on `06817109`.
Both candidate app slots were hash-verified. Image SHA-256
`76166ce106e53e5bfdff7338ecda314beb1bc303edcc813dd040630076807931`.
Bench evidence is in `utils/bench_baselines/batch-06/`.


## Batch 7 · Developer Vario CSV and bus log

| Audit item | Change | Memory accounting |
| --- | --- | --- |
| I04 | Retire the Developer Vario CSV toggle, API field and writer. Remove the CSV-only IMU fields and long climb average. Store the two factory timeout readings at the precision their getters already return. | The linked IMU is **72 B** smaller (624 to 552 B), and the linked barometer is **8 B** smaller (512 to 504 B). Total linked static RAM fell from 98,332 to 98,252 B. Retiring the writer also removes its temporary SD/file allocations and periodic writes when enabled. A USB sample between writes does not isolate that temporary allocation. |
| S17b | When Developer mode turns off, stop the bus log. Unsubscribe under the message-bus lock before closing the file so an already-dispatched callback can finish. | On the candidate the same System menu checkpoint gained **460 B** when Developer mode was switched off. The older build gained 8 B and continued logging. Its separate open step consumed 4,432 B versus 548 B on the candidate. These heap readings include allocator and SD effects. |
| S17c | Allow startup bus logging only with Developer mode enabled, and start after SD and the message bus are available. | Avoids opening the same file and buffers in ordinary mode. There is no separate steady-state reduction when startup logging is disabled already. |

| USB checkpoint | Batch 6 free B | Batch 7 free B |
| --- | ---: | ---: |
| Developer menu before manual start | 58,356 | 59,040 |
| Developer menu with bus log running | 53,924 | 58,492 |
| System menu before disabling Developer mode | 53,788 | 58,356 |
| System menu after disabling Developer mode | 53,796 | 58,816 |
| System menu after re-enabling Developer mode | 53,796 | 58,824 |

Each value is the median of five USB reads after a one-second settle. The
candidate console confirmed that the bus log closed on Developer mode exit
and stayed closed on re-entry. A saved startup preference also started the
bus log after a USB reset and wake with Developer mode enabled. Both
candidate logs contained the firmware header and pressure records. Simulator
cold boots with the same legacy startup flag started a log only with
Developer mode enabled. The simulator also passed live vario filter
continuity on both IMU paths, Developer logging and runtime capture. The
bench reported a ready vario and HTTP 200 for live vario settings. The
interactive factory self-test remains a hardware follow-up.

The baseline briefly enabled Vario CSV for comparison. The subsequent
candidate preserved that file byte for byte and omitted its control and API
field. The test removed the three disposable bus logs, preserved the
pre-existing bus logs, and restored the original Developer mode and startup
logging settings.

The final image was `0.2.4-756d.bench+h3.2.5`, built on `7568b4c6`. Both
app slots were hash-verified. Image SHA-256
`01efe79fd25da0685830072a9bd3cdf19bb011439e77286620c243a24a69ff2d`.
Bench evidence is in `utils/bench_baselines/batch-07/`.

## Batch 8 · Unused USB device classes

Leaf compiles the device dispatcher from the TinyUSB revision pinned by the
Arduino SDK. The linker then omits the unused class drivers and keeps the
SDK's USB controller, control endpoint, serial (CDC), and mass storage
(MSC). The CDC endpoint buffers still total 128 B and the MSC endpoint
buffer is still 4,096 B. This also retains Developer firmware mass storage.

| Audit item | Disabled TinyUSB class | Linked static class storage removed |
| --- | --- | ---: |
| LIB-058 | NCM networking | 6,472 B |
| LIB-059 | Application DFU and DFU runtime | 4,116 B |
| LIB-060 | HID | 204 B |
| LIB-061 | MIDI | 496 B |
| LIB-062 | Video | 153 B |
| LIB-063 | Vendor interface | 500 B |

The class symbols account for 11,941 B. The net linked static RAM saving
was **11,928 B**, including the dispatcher's layout and section alignment.
Both builds used the same pinned SDK and boot configuration.

| Leaf 3.2.5 bench metric | Batch 7 | Batch 8 | Change |
| --- | ---: | ---: | ---: |
| Linked static RAM | 98,252 B | 86,324 B | −11,928 B |
| Free internal heap before opening the menu | 111,412 B | 123,340 B | +11,928 B |
| Largest free internal block at idle | 49,140 B | 61,428 B | +12,288 B |
| Minimum free heap since boot at that checkpoint | 102,400 B | 114,328 B | +11,928 B |

Each idle reading is the median of ten samples on the same unit after a
reset and USB wake. Both measurements used the User page, the saved radio
configuration, and a closed Web App.

Windows enumerated the serial console, Leaf SD volume, and Developer
firmware mass-storage device. Both builds flushed a 2 MiB SD write and
matched its SHA-256 on readback. The disposable file was removed. Windows
accepted explicit SD eject on both builds and showed the SD media absent.
The bench console stopped replying after eject on both builds. The
candidate re-entered the ESP32 bootloader over USB, and firmware SD access
and the console returned after reset and wake. Both app slots were flash
hash-verified. USB serial and Leaf Log simulator regressions also passed.

The regular `leaf_3_2_5_release` build compiled and linked with the same
dispatcher. Remaining hardware coverage includes an actual update through
the Developer firmware drive, production-image USB behavior on other
hardware revisions, and a fresh factory commissioning run.

The tested bench image was `0.2.4-632d.bench+h3.2.5`, built on `63205415`.
Image SHA-256
`b9f4df22485e213568c51a6e97c31e80b82e1ab9d4c4b090617410ca0fa40441`.
Bench evidence is in `utils/bench_baselines/batch-08/`.

## Batch 9 · Flight cleanup and TLS experiment

| Audit item | Change | Memory result on 3.2.5 |
| --- | --- | --- |
| S09a | Free the eight header-only strings immediately after emitting the IGC header. Rebuild them when the next flight starts. | String capacities depend on the profile and version. With the simulator's two roughly 40-character profile values and the bench firmware version and MAC, the pinned Arduino allocator would request about **160 B** of string buffers. An isolated hardware heap delta is pending a GPS-synced flight. |
| UI-06-B | Destroy the in-memory summary and both delete paths when the modal is removed. | Free internal heap increased **724 B** across dismissal of a stationary 13-second flight summary. This is a whole-device checkpoint delta. The device saved and then removed the disposable logbook record. The run had no GPS fix and did not write an IGC. |
| UI-08-B | Release scan paths when the scan completes and destroy the summary and current path on exit. Back and global pop use the same cleanup. | Closing Logbook returned **440 B** and **444 B** on two successive cycles in the stock-TLS cleanup build. The previous image returned 0 B and lost another 136 B on the corresponding closes. Measured with existing logbook entries and saved Wi-Fi reconnect active in the main menu. |
| LIB-001 | On `leaf_3_2_5_tlsbench`, reduce the outbound TLS record payload from 16 KiB to 4 KiB and keep 16 KiB inbound. | TLS context setup used **12,288 B less internal heap**, dropping from 35,200 B to 22,912 B. The incoming buffer stays 16 KiB. The allocation exists for an open TLS context. |

The simulator passed the logbook I/O and maintenance, charging Leaf Log sync,
USB serial, and runtime capture suites. Consecutive IGC recordings verified
long, changed, and missing pilot/glider profiles. The device passed the Web
App's HTTPS firmware availability check and exported a short runtime trace.
The final eight-second trace used the Web App with the radio workers
suspended. It recorded 784 deadline misses and 16 skipped ticks with no
matched timing baseline. The trace verifies capture and export and requires
follow-up under the normal on-state workload.

| Task | Allocated stack | Smallest observed unused stack | Workload |
| --- | ---: | ---: | --- |
| Arduino loop | 12,288 B | 7,264 B | Stationary flight timer |
| BLE worker | 5,120 B | 2,892 B | Stationary flight timer |
| FANET TX worker | 4,096 B | 2,760 B | Stationary flight timer |
| FANET RX worker | 4,096 B | 3,352 B | Stationary flight timer |
| Leaf Log upload worker | 12,288 B | 7,288 B | Eight live IGC uploads |

Repeat the on-state measurements with a GPS fix, active navigation, IGC
writes, radio peers, and a BLE client before selecting stack reductions.
Repeat the upload stack measurement under network interruption and retry.
See [USB bench console](usb-bench-console.md#compare-tls-record-buffers).

The stock-TLS and experimental bench builds both used 86,332 B of linked
static RAM and showed 123,332 B of idle internal heap. The experiment added
1,400 B of linked flash. The ordinary 3.2.5 release build passed with stock TLS.
Both bench images were verified in both OTA app slots of the 3.2.5.
The final TLS image was `0.2.4-865d.tlsbench+h3.2.5` with SHA-256
`cf58b086ff21ea8130398beafe3878a946d55dd3393668f69e62f519c0a9b83b`.
Bench evidence is in `utils/bench_baselines/batch-09/`.

### Live Leaf Log upload on 3.2.5

The 4 KiB TLS bench image reconnected to saved Wi-Fi in charging mode and
stored eight distinct server flight IDs for the eight existing pending IGCs.
The IGCs totaled 1,345,238 B, with the largest at 595,226 B. Windows mounted
the SD card roughly 40 seconds after the first charging sample. No manual
logbook edits were needed. On a second charging cycle, all deliveries were
still recorded and the samples showed neither Wi-Fi nor an upload worker.

The lowest sampled free internal heap during the connected phase was
45,004 B and the boot-wide minimum reached 32,652 B during the session.
The upload stack used at most 5,000 B of its 12,288 B allocation in this run.
This was a successful transfers check on the TLS image. Interrupted
transfers and their retries remain open.

### On-device Runtime start with Bluetooth and FANET

The first GPS field attempt showed `ERR`. With the flight timer running,
Bluetooth and FANET active, and background menu Wi-Fi connected, the bench
console measured 52,704 B free and a largest free block of 31,732 B. The
recorder required 55,264 B in total, including its 32 KiB reserve.

Menu-started capture now releases background Wi-Fi when the Web App is
closed. Reopening the menu during capture skips the automatic Wi-Fi
connection. On the 3.2.5 bench, the updated firmware started and exported a
12-second stationary flight-timer capture with Bluetooth and both FANET
workers active. Sampled internal free heap stayed at or above 69,472 B. The
GPS-synced workload remains to be captured.

Runtime menu verification is recorded in
`utils/bench_baselines/batch-09/runtime-menu-verification.json`.

A second attempt showed `ERR` with Wi-Fi already off. The console measured
86,908 B free, with a largest block of 21,492 B. The recorder previously
needed a single 22,496 B block. It now allocates the per-slot statistics
separately, using blocks of 12,800 B and 9,704 B with the same 32 KiB reserve.
The simulator verified capture with a 16 KiB allocation cap and cleanup if
the second allocation fails. The updated 3.2.5 firmware exported a
12-second stationary capture with Bluetooth and both FANET workers active.
The outdoor run below verified capture and export with a GPS fix.

Split-allocation evidence is in
`utils/bench_baselines/batch-09/runtime-split-verification.json`.

### GPS-synced on-state capture

The 3.2.5 split-allocation image exported a 163.955-second Runtime capture.
The main IGC covered 91 seconds and contained 91 GPS fix records. The second
IGC covered four seconds and contained five fixes. Both contained the
checked IGC headers and end record and matched their G-record checksum.

With the 22,504 B recorder allocated, sampled free internal heap stayed at
or above 64,560 B and the smallest sampled largest block was 31,732 B.
The boot-wide heap minimum of 36,876 B was already present at the first
sample. The recorder observed zero allocation failures.

| Task | Stack allocation | Minimum unused in the capture | Observed use |
| --- | ---: | ---: | ---: |
| Arduino loop | 12,288 B | 7,684 B | 4,604 B |
| BLE worker | 5,120 B | 2,988 B | 2,132 B |
| FANET TX worker | 4,096 B | 2,132 B | 1,964 B |
| FANET RX worker | 4,096 B | 1,960 B | 2,136 B |

The first memory sample included both FANET workers. Later samples include
periods after the flight timer stopped. Bluetooth client traffic and FANET
peer traffic were not independently verified. An initial loop-stack
experiment at 10 KiB would save 2 KiB of on-state heap. Verify the heavier
flight, navigation, SD, and network workloads before adopting any reduction.

The report recorded 474 deadline misses and 34 coalesced ticks in normal
slots. The longest occupied slot was 219 ms in the IGC logging slot.
This identifies which slot to investigate. It does not isolate the SD write
or establish a timing regression without a matched capture.
See `utils/bench_baselines/batch-09/outdoor-runtime-summary.json`.

### Longer stack measurements

The Developer **Stacks** monitor samples the Arduino loop, BLE worker, and
FANET transmit and receive workers every 30 seconds. It also samples a
registered worker before deletion to preserve low readings from instances
that start and stop between periodic samples. It keeps one counter and
minimum per task and allocates no heap buffer. The 3.2.5 bench build adds
24 B of linked static RAM compared with the preceding bench image. The
simulator verified a three-hour session, worker restarts, and save retry
after an unavailable SD card.

The plugged-in 3.2.5 saved a 36.6-second CSV with two instances each of
FANET TX and RX. The saved minima were 7,836 B for the loop, 2,996 B for
BLE, 2,616 B for FANET TX, and 3,344 B for FANET RX. We read the CSV
back over USB. See `utils/bench_baselines/batch-09/stack-monitor-device.csv`.

The next build also saved an active stack session on normal shutdown. We
started monitoring, turned off the plugged-in 3.2.5, and read the 6.78-second
CSV from USB storage. The simulator verified normal and low-battery saves
within the current shutdown deadline. This change added no linked static RAM.
See `utils/bench_baselines/batch-09/stack-shutdown-device.csv`.

The 12 KiB loop stack remains in use. The audit lists 10 KiB as the first
trial after broader workload coverage, with 2 KiB of potential on-state
heap savings. See [how to record a stack session](runtime-assessment.md#monitor-stack-headroom-during-regular-use).

### Release build and flash on 3.2.6

The `leaf_3_2_6_release` build includes the stack monitor and shutdown
autosave, uses stock TLS, and links 86,124 B of static RAM. Both OTA app
slots verified after writing `0.2.4-865d+h3.2.6`. The firmware rebooted,
the same USB device reappeared, and Windows mounted its SD card.
Shutdown CSV readback was verified on the 3.2.5 bench. The 3.2.6 check
covered flash and boot. See
`utils/bench_baselines/batch-09/leaf-3.2.6-release-flash.json`.
