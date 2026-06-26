---
title: Logbook Path Distance Backlog
description: Future correction for along-path distance metrics in logbook entries.
---

# Logbook Path Distance Backlog

Leaf currently estimates logbook `metrics.path_distance_m` by accumulating GPS-reported ground speed once per second. This can undercount, especially at low speeds or when GPS speed is noisy/quantized. In one stationary test, `path_distance_m` was smaller than `straight_line_distance_m`, which should generally not happen for a true along-path distance.

Future work:

- Track the previous valid GPS point during each flight.
- Accumulate path distance from point-to-point GPS distance between consecutive valid fixes.
- Keep `straight_line_distance_m` as the distance between first fix/start location and end location.
- Consider whether very small jitter movements should be filtered or thresholded for stationary/near-stationary logs.
