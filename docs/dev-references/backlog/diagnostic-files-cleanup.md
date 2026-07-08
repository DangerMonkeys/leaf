---
title: Diagnostic Files Cleanup Backlog
description: Reference and future cleanup notes for dev-mode SD-card diagnostic files.
---

# Diagnostic Files Cleanup Backlog

Leaf can write diagnostic files under `/diagnostics` on the SD card when the `dev_mode` user setting is enabled. These files are intended for development and field debugging; normal user mode should not create the diagnostics directory or write these files.

Current diagnostic files:

- `/diagnostics/heap_lifecycle.csv`
  - Written by `heap_monitor::checkpoint()` as major firmware lifecycle events occur.
  - CSV columns: `millis`, `event`, heap totals, largest allocatable blocks, internal/PSRAM availability, current task, and loop/BLE/FANET stack high-water marks.
- `/diagnostics/heap_monitor.csv`
  - Written by `heap_monitor::dumpToSd()` when a buffered heap snapshot is flushed, currently around user web-app shutdown.
  - Uses the same CSV format as `heap_lifecycle.csv`; rows come from the recent in-memory ring buffer instead of only immediate lifecycle checkpoints.
- `/diagnostics/wifi_setup.csv`
  - Written during the Leaf AP/web-app WiFi setup flow.
  - CSV columns include setup cycle, portal/provisioning/connection state, WiFi status and mode, AP station count, local/AP IPs, current and target SSIDs, connection errors, connection timing, and heap availability.
- `/diagnostics/wifi_autoconnect.csv`
  - Written by the saved-network WiFi coordinator while attempting automatic station connections.
  - CSV columns include attempt cycle/index/count, target/current SSID, WiFi status and mode, RSSI, local IP, whether diagnostic-network activity is still allowed, heap availability, and current task.
- `/diagnostics/web_requests.csv`
  - Written around user web-app route handlers.
  - CSV columns include request sequence, route label, HTTP method and URI, start/duration timing, heap availability, WiFi status/mode/RSSI, AP station count, provisioning state, and current task.
- `/diagnostics/webapp_requests.csv`
  - Written when the user web app is disabled to summarize route/counter activity.
  - CSV columns include user-app enabled/configuration state, AP station peak, WiFi status, heap availability, loop/handle counts, per-route counters, not-found count, and AP IP.
- `/diagnostics/boot_report.csv`
  - Written once per boot after SD mount.
  - CSV columns are `millis`, `section`, `key`, `value`, and `detail`; current sections cover reset reason and navigation structure/capacity sizes.

Future work:

- Decide whether these SD-card diagnostics should stay as developer tools, move behind a stronger build/debug switch, or be removed once the related release hardening is complete.
- If they stay, consider individual `dev_mode` toggles for heap, WiFi setup, WiFi autoconnect, web request, web-app counter, and boot reports.
- Add a small web-app/debug UI for clearing or downloading diagnostic files so field testers do not need to inspect the SD card manually.
- Consider rotating or truncating long-running CSV files to avoid unbounded SD growth during repeated development sessions.
- Keep any lightweight runtime checks that are required for correct behavior, even if their file-writing diagnostics are removed.
