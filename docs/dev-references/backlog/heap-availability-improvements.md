---
title: Heap Availability Improvements Backlog
description: Ordered backlog for improving Leaf's general heap availability and reducing fragmentation.
---

# Heap Availability Improvements Backlog

Recent heap diagnostics showed that Leaf has substantial heap available shortly after SD mount, but much of it was consumed or fragmented before memory-intensive features like WiFi setup, the web app, OTA checks, route editing, and waypoint transfer ran. Recent memory work has improved the major web-app and lifecycle pressure points enough that this backlog is no longer an urgent failure list. It is now a pickup note for cautious follow-up if real-world diagnostics show more room to squeeze.

Completed or mostly completed work:

1. Diagnostic WiFi scan gating.
   - `diagnostic_network.update()` previously explained a roughly 49 KB post-setup heap drop by initializing WiFi STA mode and starting an async scan.
   - Diagnostic network scanning is now limited to commissioning/factory-relevant states through `settings.diagnosticNetworkScanAllowed()`, so fully commissioned units should not pay this cost during normal use or web-app testing.
   - If this area is revisited, focus on commissioning-pending and first-boot behavior, not normal user runtime.

2. Radio and service lifecycle boundaries.
   - User web-app and WiFi setup flows now have clearer lifecycle boundaries and diagnostics around setup portal shutdown, user-app startup, and service resume.
   - BLE is released for memory-intensive web/OTA paths where appropriate; OTA diagnostics showed `BLE::end()` can recover roughly 60 KB.
   - The captive-portal-to-network-web-app flow was tightened so setup teardown completes before later user-app activity, without auto-loading the web app after setup.

3. Web response streaming and bounded builders.
   - Large static `/app` and `/wifi` HTML responses use flash-backed send paths instead of heap-backed `String` copies.
   - Worst nav/web responses were streamed or chunked so waypoint/nav data no longer requires large monolithic JSON strings.
   - `/api/user-waypoints` and related waypoint responses were optimized to reduce latency and heap churn.
   - Recurring JSON builders use tighter reserve/lifetime patterns where they were clearly hot.

4. KML removal.
   - KML track logging has been removed; IGC is now the canonical flight track record for new flights.
   - KML waypoint loading has been removed because it previously read the whole file into one heap-backed `String` before parsing.
   - The web app and device file pickers no longer list `.kml` files for waypoint import.

5. Static data and templates.
   - The main web app and setup HTML are served from flash-backed storage where practical.
   - This should continue to be checked with release-build `.data`/`.bss` reports, but the largest static web assets have already been addressed.

Remaining possible follow-up items:

1. Audit and reduce task stack reservations.
   - Use real-world heap lifecycle stack high-water logs to size each FreeRTOS task closer to observed use.
   - Review loop, BLE, FANET TX, FANET RX, web/server-related work, and any other long-lived task stacks.
   - Keep a conservative safety margin, but return obvious excess stack reservations to general heap.
   - This is the best next candidate if hours of dev-mode logging show consistently large unused stack margins.

2. Selective additional web response streaming.
   - Prefer `sendContent()`/streaming writers over building full JSON/HTML responses in heap-backed `String` objects when response size grows with SD-card content.
   - Remaining candidates include profiles, logbook entries/details, route editor data, and OTA/version responses.
   - Do this selectively based on measured web-app pressure; avoid turning small fixed responses into complex streaming code.

3. Replace hot-path dynamic `String` usage with fixed buffers or reusable scratch storage.
   - Audit code that repeatedly appends to `String`, calls `reserve()`, or builds temporary JSON in request handlers and parsers.
   - Use fixed `char[]` buffers, bounded formatting, reusable module buffers, or streaming output where lifetimes are simple.
   - Prioritize paths visible in diagnostics: profiles, route save/import, logbook entry/detail, OTA, and WiFi setup.

4. Audit ArduinoJson document sizing and lifetimes.
   - Identify large `JsonDocument` allocations in profile, waypoint, route, logbook, web app, and setup paths.
   - Replace full-document reads with streaming, filtered parsing, or narrower documents where practical.
   - Ensure documents are scoped tightly so their heap is released before response construction begins.

5. Review fixed-size navigation and route storage.
   - `maxNavPoints`, `maxRoutePointRefs`, and related fixed arrays provide predictable behavior but reserve RAM permanently.
   - Decide whether current capacities are worth their always-on cost, or whether some route/waypoint data can live on SD and be paged or streamed.
   - Keep flight-critical navigation deterministic; avoid heap-heavy dynamic containers in the active flight path.

6. Defer optional features until first use.
    - Avoid initializing web app, OTA, route editing, profile editing, or heavy diagnostics until the user enters the relevant mode.
    - Consider lightweight placeholders at boot, with explicit init/deinit around each feature session.
    - Re-test repeated enter/exit cycles to ensure heap returns close to the prior baseline.

7. Keep heap diagnostics as a regression tool while optimizing.
    - Preserve lifecycle checkpoints during this work and use `/diagnostics/heap_lifecycle.csv` as the primary before/after evidence.
    - Track free heap, largest free block, minimum free heap, current task, and stack high-water values.
    - Use extended real-world dev-mode logs before reducing stack reservations or making further lifecycle changes.

8. Consider memory policy and feature gating for low-heap states.
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
- Earlier KML parsing and nav-load experiments helped isolate heap behavior; KML import has since been removed to avoid whole-file heap allocations.
- After the web/nav streaming, WiFi lifecycle, and KML-removal work, release-build size was roughly 98.8 KB RAM usage and 2.11 MB flash usage. Hardware smoke testing showed the web app responsive again; longer real-world dev-mode logs should guide any further stack or lifecycle tuning.
