---
title: Web App Firmware Check Follow-up
description: Current behavior and future options for checking firmware updates from the Leaf web app.
---

# Web App Firmware Check Follow-up

Leaf now includes a user-triggered firmware update check in the hosted web app. In network web
app mode, the Status card shows a `Check for Updates` button under the battery information. Leaf
AP mode hides the button because that mode does not provide internet access.

The button calls `POST /api/firmware/update-status`, which reuses the same metadata check as the
device `Settings > System > Update FW` menu: `getLatestTagVersion()` from
`src/vario/comms/ota.cpp`. The endpoint fetches:

```text
https://github.com/DangerMonkeys/leaf/releases/latest/download/latest_versions.json
```

The web-app endpoint only reports status. It never starts an OTA update. Current success messages
are:

- `Leaf is up to date.`
- `New version available! To update go to Settings>System>Update FW`

The manual check was added after web-app startup began unloading BLE, which gives HTTPS enough
runtime headroom. A representative dev-mode test did this sequence:

1. Opened the network web app.
2. Checked for updates and saw `Leaf is up to date.`
3. Loaded waypoint files, created a route, deleted a log, and continued using the web app.
4. Checked for updates again and saw `Leaf is up to date.`
5. Closed the web app and saved diagnostics to `scratch/memory_logs/`.

Both checks succeeded and reached `ota-version-http-ok`. The largest allocatable block dropped to
roughly 14.8 KB during the GitHub HTTPS request, then recovered to roughly 31.7 KB afterward. Free
heap recovered from roughly 43-45 KB during HTTPS back to roughly 93-95 KB after each check. This
suggests the explicit button path is viable, but HTTPS is still a memory-intensive action and
should stay user-triggered for now.

Earlier passive experiment:

- The first attempt tried to show an automatic firmware status line when the web app loaded.
- It failed while the web app, WiFi server, BLE, and normal runtime services were all active.
- Both the default `HTTPClient::begin(url)` path and an explicit insecure `WiFiClientSecure`
  path failed with `HTTP GET failed -1 (connection refused)`.
- Representative headroom before BLE unloading was only about 40 KB free heap and about 30 KB
  largest allocatable block.

Future work options:

- Keep the current button-only check unless repeated field diagnostics show enough margin for a
  passive once-per-session check.
- Consider caching the successful check result for the active web-app session so repeated button
  presses do not always hit GitHub.
- Consider a future web-app update flow that can start the firmware upgrade directly from the web
  app. That would need to mirror the dedicated update menu lifecycle: unload or pause services,
  make the metadata and binary HTTPS requests, present clear reboot expectations, and handle
  browser disconnects while Leaf restarts.
- Consider a lighter update metadata source if GitHub HTTPS remains too heavy for broader use.
