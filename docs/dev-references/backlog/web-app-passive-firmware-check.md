---
title: Web App Passive Firmware Check Backlog
description: Notes from the attempted passive latest-firmware check in the Leaf web app.
---

# Web App Passive Firmware Check Backlog

We explored showing a passive firmware status line in the Leaf web app, such as `Up to date` or `Update Available`, by reusing the existing OTA version metadata check. The desired behavior was:

- Only check when the web app is served over an existing outside WiFi network.
- Do not check when the web app is served from Leaf AP mode.
- Check only once per web-app session, then cache the result.
- Show hardware before firmware in the status block, with the update result below firmware in Leaf hero green.

The existing firmware-update menu uses `getLatestTagVersion()` from `src/vario/comms/ota.cpp`, which fetches:

```text
https://github.com/DangerMonkeys/leaf/releases/latest/download/latest_versions.json
```

That path works as part of the dedicated `Update FW` flow, but that flow explicitly unloads BLE first because OTA checking/updating is memory-expensive. The passive web-app experiment tried to perform only the metadata check while the web app, WiFi server, BLE, and normal runtime services were still active.

Tested approaches:

- Added a separate `/api/firmware/update-status` route so `/api/user/status` stayed lightweight.
- Cached the result for the active web-app session so the browser would not repeatedly hit GitHub.
- Added heap/max-allocation instrumentation around `getLatestTagVersion()`.
- Improved OTA error reporting using `HTTPClient::errorToString()`.
- Tried the default `HTTPClient::begin(url)` path.
- Tried an explicit TLS client:

```cpp
WiFiClientSecure client;
client.setInsecure();
HTTPClient http;
http.begin(client, LeafVersionInfo::otaVersionsUrl());
```

Both HTTP client approaches failed from the web app path with:

```text
HTTP GET failed -1 (connection refused)
```

Representative diagnostic values:

```text
free heap before: 40552-40732 bytes
free heap after: 40032-40200 bytes
max alloc before: 29684-31732 bytes
max alloc after: 29684-31732 bytes
```

Interpretation:

- The passive check likely fails because GitHub HTTPS/TLS needs more contiguous runtime headroom than is available while the full web-app stack and other services are active.
- The failure does not leave a large net heap loss, but the available contiguous allocation around 30 KB appears too small for this request path.
- Explicit `WiFiClientSecure` did not solve the failure.

Future work options:

- Keep update availability checks in the dedicated `Update FW` menu only, where unloading BLE and rebooting are already expected.
- If web-app status remains desired, add a deliberate "Check for Updates" action that warns the user it may temporarily unload BLE or pause services.
- Explore a web-app path that mirrors the firmware-update menu lifecycle: unload BLE, stop/pause nonessential services, perform the metadata check, then require reboot or controlled service reinitialization.
- Consider a lighter update metadata source in the future if one can be served without GitHub HTTPS/TLS overhead.
- If revisiting implementation, start from a clean branch and re-add only the status route, one-shot cache, heap instrumentation, and UI line after confirming the service-lifecycle strategy.
