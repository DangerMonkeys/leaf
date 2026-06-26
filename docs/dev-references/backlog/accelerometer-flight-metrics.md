---
title: Accelerometer Flight Metrics Backlog
description: Future improvements for using accelerometer data in flight summaries and logbook entries.
---

# Accelerometer Flight Metrics Backlog

Leaf currently tracks max/min acceleration using the same value displayed in the optional thermal-page user field. That is acceptable for the first logbook schema, but the sampled value can miss peaks between once-per-second log updates and can also capture short non-flight shocks, such as the device being bumped.

Future work:

- Track a running 0.5 second average of acceleration magnitude for flight-summary and logbook metrics.
- Reject or ignore very high-slope transient spikes that look like knocks or handling impacts rather than sustained in-flight g load.
- Use the filtered flight-relevant acceleration value for `metrics.max_accel_g` and `metrics.min_accel_g`.
- Keep the raw/instantaneous accelerometer behavior available for diagnostics if it remains useful elsewhere.
