---
title: Heap Availability Improvements Backlog
description: Ordered backlog for improving Leaf's general heap availability and reducing fragmentation.
---

# Heap Availability Improvements Backlog

Recent heap diagnostics showed that Leaf has substantial heap available shortly after SD mount, but much of it is consumed or fragmented before memory-intensive features like WiFi setup, the web app, OTA checks, route editing, and waypoint transfer run. The goal of this backlog is to improve Leaf's general heap headroom, not just fix one acute web-app request.

Approximate order of attack:

1. Gate diagnostic WiFi scanning so it does not consume normal runtime heap.
   - Later lifecycle diagnostics identified the previously unexplained post-setup heap drop: the first `diagnostic_network.update()` call can consume roughly 49 KB by initializing WiFi STA mode and starting an async scan.
   - Avoid running production/diagnostic-network scanning during ordinary user operation, web-app testing, and memory profiling unless explicitly requested or factory/commissioning conditions are active.
   - Confirm that when diagnostic scanning is skipped, Leaf reaches the WiFi menu/web-app path with the roughly 49 KB baseline heap restored.
   - If the scan is still needed in some USB-powered states, ensure it calls `WiFi.scanDelete()` and returns the WiFi stack to a low-memory/off state when no diagnostic network is found.

2. Tighten radio and service lifecycle boundaries.
   - Make WiFi setup, the user web app, BLE, FANET, OTA checks, and other network-adjacent features more mutually exclusive where possible.
   - When entering WiFi setup or web-app mode, release unneeded BLE/FANET resources instead of only stopping advertising or pausing activity.
   - Confirm task handles, queues, protocol objects, and buffers are actually destroyed when a feature is no longer active.
   - Recent OTA diagnostics showed `BLE::end()` recovered roughly 60 KB, confirming BLE is a major reclaimable subsystem when web/OTA features need headroom.

3. Audit and reduce task stack reservations.
   - Use heap lifecycle stack high-water logs to size each FreeRTOS task closer to observed use.
   - Review loop, BLE, FANET TX, FANET RX, web/server-related work, and any other long-lived task stacks.
   - Keep safety margin, but return oversized stack reservations to general heap.

4. Reduce WiFi setup overlap and cleanup latency.
   - Avoid overlapping AP, DNS, scanning, STA connect, setup web server, and user web app longer than necessary.
   - Tear down AP/DNS/setup handlers before or immediately after STA connection when the user flow allows it.
   - Delete WiFi scan results aggressively and verify that scan/connect paths return heap and largest-block headroom.

5. Stream or chunk web responses by default.
   - Prefer `sendContent()`/streaming writers over building full JSON/HTML responses in heap-backed `String` objects.
   - Apply first to waypoint/nav data, user waypoints, profiles, logbook entries, route editor data, OTA/version responses, and any response that grows with SD-card content.
   - Preserve clear low-heap responses so the UI can report incomplete data instead of appearing random or truncated.

6. Replace hot-path dynamic `String` usage with fixed buffers or reusable scratch storage.
   - Audit code that repeatedly appends to `String`, calls `reserve()`, or builds temporary JSON in request handlers and parsers.
   - Use fixed `char[]` buffers, bounded formatting, reusable module buffers, or streaming output where lifetimes are simple.
   - Prioritize paths visible in diagnostics: profiles, nav data, route save/import, logbook entry, OTA, and WiFi setup.
   - Consider removing KML track logging now that IGC is the richer canonical flight record and Leaf Log can analyze uploaded IGC files; the direct always-on RAM saving is small, but it simplifies the logging surface and removes end-of-flight KML formatting churn.

7. Audit ArduinoJson document sizing and lifetimes.
   - Identify large `JsonDocument` allocations in profile, waypoint, route, logbook, web app, and setup paths.
   - Replace full-document reads with streaming, filtered parsing, or narrower documents where possible.
   - Ensure documents are scoped tightly so their heap is released before response construction begins.
   - KML waypoint loading currently reads the whole file into one heap-backed `String` before parsing; either remove KML waypoint import support along with KML logging or rewrite it as a streaming parser before treating KML as safe for low-heap web/app workflows.

8. Move static data and templates out of RAM.
   - Keep constant web app assets, HTML/CSS/JS fragments, labels, lookup tables, and diagnostic strings in flash/PROGMEM where practical.
   - Continue using `send_P` or equivalent flash-backed response helpers for large static web content.
   - Check static RAM reports after each change to confirm `.bss`/`.data` does not grow unexpectedly.

9. Review fixed-size navigation and route storage.
   - `maxNavPoints`, `maxRoutePointRefs`, and related fixed arrays provide predictable behavior but reserve RAM permanently.
   - Decide whether current capacities are worth their always-on cost, or whether some route/waypoint data can live on SD and be paged or streamed.
   - Keep flight-critical navigation deterministic; avoid heap-heavy dynamic containers in the active flight path.

10. Defer optional features until first use.
    - Avoid initializing web app, OTA, route editing, profile editing, or heavy diagnostics until the user enters the relevant mode.
    - Consider lightweight placeholders at boot, with explicit init/deinit around each feature session.
    - Re-test repeated enter/exit cycles to ensure heap returns close to the prior baseline.

11. Keep heap diagnostics as a regression tool while optimizing.
    - Preserve lifecycle checkpoints during this work and use `/diagnostics/heap_lifecycle.csv` as the primary before/after evidence.
    - Track free heap, largest free block, minimum free heap, current task, and stack high-water values.
    - Add targeted checkpoints before removing temporary diagnostics, especially around the currently unattributed baseline drops.

12. Consider memory policy and feature gating for low-heap states.
    - Define minimum heap and largest-block thresholds for memory-intensive actions.
    - When below threshold, degrade gracefully by skipping optional details, streaming smaller batches, or asking the user to retry after closing a mode.
    - Treat this as a guardrail, not a substitute for reducing baseline memory use.

Useful evidence from the first heap lifecycle pass:

- SD mount started around 186 KB free heap with a 122 KB largest block.
- Setup plus task/BLE initialization reduced free heap to roughly 92 KB.
- A later diagnostic pass explained the missing post-setup drop: `diagnostic_network.update()` reduced free heap from roughly 92 KB to roughly 43 KB, a nearly 49 KB cost from early WiFi scan initialization.
- BLE setup consumed roughly 61 KB total, and `BLE::end()` recovered about 60 KB before OTA checks.
- WiFi setup/connect drove the recorded minimum free heap as low as roughly 700 bytes.
- Some nav/web responses ran with largest free blocks below 4 KB, which is enough to cause fragmentation-sensitive failures; one `nav-data` response took roughly 200 seconds and ended with only about 2.3 KB largest allocatable block.
- After reboot, KML parsing and nav load showed much healthier heap, suggesting lifecycle cleanup and baseline reduction should have broad impact.
