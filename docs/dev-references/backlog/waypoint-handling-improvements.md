---
title: Waypoint Handling Improvements Backlog
description: Follow-up ideas for Leaf waypoint naming, selection, route handling, and future memory reductions.
---

# Waypoint Handling Improvements Backlog

Recent waypoint memory work kept the simple in-RAM waypoint model, reduced fixed waypoint name
length, and added faster hold-scroll behavior for long destination lists. The current structure is
working well enough that deeper SD-backed waypoint paging should be treated as optional future work,
not an urgent rewrite.

Completed or mostly completed work:

1. Keep the simple RAM model for now.
   - Leaf currently stores loaded waypoint details in `navigator.waypoints[]`.
   - Routes remain compact: route points store waypoint indexes plus route metadata, not full
     duplicated waypoint details.
   - This keeps active navigation, route sequencing, and IGC task declaration fast and predictable.

2. Reduce fixed waypoint name length.
   - `maxGpxNameLength` was reduced to 15.
   - User waypoint rename in the web app now enforces the same 15-character limit while typing.
   - The current waypoint array is expected to be roughly 3.4 KB for 120 loaded points.

3. Fast-scroll long destination lists on Leaf.
   - The waypoint/route destination selector now responds to held up/down input.
   - Normal click scrolling still wraps as before.
   - Held scrolling moves by a visible page and clamps at the first/last item so users can feel the
     list boundary.

Remaining possible follow-up items:

1. Prefer short CUP names on Leaf.
   - CUP files often include a long descriptive name in field 0 and a shorter code in field 1.
   - Leaf currently stores/displays field 0 for plain CUP waypoints.
   - Consider using field 1 as the Leaf display name when present, while preserving field 0 for web
     app detail if needed.
   - Example: `"BELLA ERBA",BELLA,...` could display as `BELLA` on Leaf.

2. Show richer CUP names in the web app.
   - If Leaf switches CUP display names to short codes, the web app could still show both forms.
   - Example display: `BELLA - Bella Erba`.
   - This would keep Leaf compact while preserving confidence for `Open Map` and route-building
     workflows.

3. Add position context to the Leaf selector.
   - Long lists would benefit from a small `current/total` indicator such as `37/120`.
   - This is especially useful now that hold-scroll can move by pages.
   - Candidate locations: title/status area or the bottom row near `Back`.

4. Consider a fixed waypoint name pool before SD-backed waypoint details.
   - Current fixed inline names waste RAM when most waypoint names are short.
   - A deterministic embedded-friendly middle path would be:
     - `WaypointCore[]` with lat/lon/elevation plus name offset/length.
     - A fixed global `waypointNamePool[]` for all loaded waypoint display names.
   - This would let short files keep longer names and long files naturally truncate more aggressively
     when the pool fills.
   - Expected savings are modest compared with a full SD-backed redesign, but complexity is also much
     lower.

5. Revisit SD-backed waypoint catalogs only if capacity becomes a product requirement.
   - The earlier SD-backed waypoint-detail attempt did not save enough RAM because dedupe metadata
     recreated much of the original waypoint table.
   - A cleaner future design would skip durable dedupe for route points, store source details in a
     normalized SD cache, and keep only a compact RAM catalog:
     - item type: waypoint or route
     - display name reference
     - SD record index/locator
     - route point count for route entries
   - Active routes would still be materialized into a small full-detail RAM buffer.
   - This is only worth doing if Leaf needs substantially more than 120 loaded waypoint entries.

6. Keep GPX route requirements simple.
   - Name-only GPX route points were a local/manual testing convenience, not standard user-facing GPX
     behavior.
   - Future parsers may reject route points without coordinates instead of keeping a name-resolution
     path just for test files.
   - The web app route builder now covers the manual route creation use case more cleanly.

7. Review route and waypoint grouping in the web app.
   - Leaf's destination selector shows routes and waypoints together, with route glyphs for clarity.
   - The web app could more clearly group loaded source-file routes separately from plain waypoints.
   - This may matter more if imported GPX/CUP files contain several routes plus many standalone
     points.

Useful current constraints and estimates:

- Current waypoint cap: 120 loaded waypoint entries plus the sentinel slot.
- Current route point cap: 40 compact route references.
- Current waypoint name cap: 15 display characters plus null terminator.
- Current `Waypoint` shape is effectively `name[16] + latE7 + lonE7 + ele`, about 28 bytes.
- Current full waypoint array is expected to be `121 * 28 = 3388` bytes.
- Route point storage is already compact and should not be the first memory target.
