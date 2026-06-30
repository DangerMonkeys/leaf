---
title: WiFi Setup Diagnostics Cleanup Backlog
description: Future cleanup for temporary WiFi setup diagnostic CSV logging.
---

# WiFi Setup Diagnostics Cleanup Backlog

Leaf currently writes temporary WiFi setup diagnostics to the SD card at:

```text
/diagnostics/wifi_setup.csv
```

This CSV logging was useful while debugging the Leaf AP web app to home-network transition path, especially around provisioning state, station connection readiness, AP station count, and web-app mode switching.

Future work:

- Remove the temporary SD-card CSV diagnostic writer once the WiFi setup transition behavior is stable.
- Keep any lightweight runtime checks that are still needed for correct AP-to-network transition behavior.
- Confirm the Leaf AP web app to WiFi setup to home-network path still redraws the Leaf display and exits AP mode after removing the diagnostic file writes.
