---
title: IGC Task Start Rendering Follow-up
description: Future compatibility check for how IGC viewers render the first route waypoint in Leaf task declarations.
---

# IGC Task Start Rendering Follow-Up

## Context

Leaf currently writes active route declarations into IGC `C` records with the first route waypoint emitted as a `STARTAREA`, intermediate points as `TURNAREA`, and the final point as `FINISHAREA`.

During v0.1.8 release testing, an online IGC viewer displayed the second and third route points but did not visibly render the first route point. The IGC file did contain the first waypoint, and the track passed through its cylinder, so this appears to be a viewer/task-interpretation issue rather than a missing-record issue.

## Follow-Up Options

For better compatibility with casual IGC viewers, consider testing one of these approaches:

1. Emit the first route waypoint as `TURNAREA` instead of `STARTAREA` when Leaf routes are being logged as navigation routes rather than formal competition tasks.
2. Duplicate the first route waypoint in the declaration as both the start area and the first turnpoint, preserving task semantics while giving viewers a normal turnpoint to render.

Before changing firmware behavior, compare the generated IGC against at least one known-good viewer and confirm that task tools still interpret the declaration acceptably.
