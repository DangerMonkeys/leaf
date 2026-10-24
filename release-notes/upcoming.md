<!--
Review date: 2026-10-24 (America/Los_Angeles).
Version is intentionally unspecified; this is an upcoming-release draft.
Base: v0.2.4 = b7d41c6ebc6f840752c5df98d5a5322c7a716892.
Target: origin/main = 3b82015ab984886dc1ba8802162570e419d3a953.
All 24 commits in this fixed range reviewed; no unpublished branch work included.
Coverage:
2e8bbb4e: release-note website metadata; internal publishing detail.
daae85f2 (#414), 82a74de7: firmware checksums and native/Windows validator.
97ff8083 (#415): thermal guidance/layout and firmware-simulator free flight/LCD tools.
a6e069a6 (#417): Display settings, automatic Core switching, configurable Nav fields.
d2c1d9a9 (#416): v3.2.8 support, bias calibration, GPS self-test and audio fixes.
2b9cb5e1 (#418): faster sliced Leaf Log preparation and session queue reuse.
9946caa8 (#419): build caches; support improvements.
c840faf9 (#420): release-only builds and explicit UDP replay opt-in.
7ed9b813: SD hardening plan; not an implemented feature by itself.
e432ee66 (#421): bounded runtime assessments, report tools, GPS UART yielding.
6c51405c (#422): linked-device Leaf Log website shortcut.
824965f4 (#423): simulator device panel/LCD controls.
dd70c883 (#424): thermal lifecycle/readiness and Basic/User naming.
9919484e: corrected hardware evidence; not an additional product change.
18fc69af (#425): animated charging UI, responsive worker, cancel confirmation, 100-flight cap.
3fb7c445 (#426): faster Thermal Track rendering and HELLO splash restoration.
9ae9fe80 (#427): backlog reconciliation and release/website checklist.
54cc79f8 (#428): arrival altitude fields, missing-elevation preservation, independent cursors.
9e0f90a6 (#429): Core altitude history and trailing-minute climb widget.
605810eb (#430): recoverable logbook saves/deletion and bounded USB diagnostics.
c7697fe8 (#431): sustained g-load, flight-aware extrema, interval IGC diagnostics.
ed39b5fe (#432): fading-score auto start and start/end diagnostics.
3b82015a: published v3.2.8 hardware schematic.
Existing Thermal Core/Track, Leaf Log uploading, the standalone browser trainer, web settings,
and USB ownership predate this range; their changes here are improvements, not first releases.
Omissions: vendored driver internals, research screenshots, test-only details, historical
measurement bundles and refactoring without independent user-visible behavior.
Qualification and limitations: docs/dev-references/release-readiness-2026-10-24.md.
-->

# Leaf upcoming release

## Leaf Vario firmware

### A. New features

- Support Leaf v3.2.8 hardware, including its new motion sensor and saved flat-position startup calibration.
- Choose four Navigate fields from nine options: time, distance, required glide and predicted arrival height for a waypoint or goal, plus current glide. Arrival height uses your averaged glide ratio; goal calculations follow the remaining route.
- Enable thermal pages in Display settings, with an optional automatic switch to Thermal Core when guidance becomes available.
- See an altitude-history graph and trailing-minute net climb above the Thermal Core map.
- Add checksums to completed IGC files so their contents can be checked for changes.
- Capture bounded timing and memory diagnostics through Developer controls for troubleshooting.

### B. Improvements

- Refined Thermal Core lift-center guidance, wind projection, breadcrumbs and steering cues, with clearer Core and Track layouts and faster Thermal Track drawing.
- Automatic flight start combines recent ground speed and climb, including climb-only launches before GPS is available. Track recording still waits for the required GPS and sensor data.
- The User page shows filtered g-load, and saved extrema use flight-aware filtering. Added IGC diagnostics help evaluate g-load and automatic flight detection.
- Faster Leaf Log preparation, up to 100 pending flights per charging session, animated progress, responsive controls and confirmation before cancelling uploads.
- Clearer charging, USB-drive and power-on messages, plus a Leaf Log website shortcut for linked devices in the Web App.
- Consistent Basic and User page names, while retaining saved page preferences.

### C. Bug fixes and stability

- More robust flight-summary saves: checked writes, automatic recovery from surviving temporary or backup records, bounded retries and visible failure messages. Deletion also removes recovery copies and associated tracks.
- Prevent a charging-to-power-on freeze when a USB serial monitor is open.
- Clear thermal session data at flight boundaries and shutdown, and guard altitude reads while the barometer wakes. Flight timer changes preserve in-progress field selections; shutdown clears them consistently.
- Preserve unknown destination heights instead of treating them as sea level. Unavailable or negative predicted arrival heights show `--`.
- Yield GPS serial reads when the next scheduled task is due.
- Correct GPS self-test satellite reporting and restore the configured vario audio when leaving the test results.
- Restore the HELLO startup splash.
- Require Developer mode and a separate opt-in before accepting UDP sensor replay.

## Other tools and support

### A. New features

- Fly an interactive thermal scenario against the real firmware in the simulator, adjust wind and lift, and save sensor recordings for replay.
- Check Leaf IGC checksums with a standalone validator, including a prebuilt Windows executable.
- Generate timing and memory reports from simulator and device captures.

### B. Improvements

- Simulator LCD zoom, pixel grid, coordinate inspection and a clearer device control panel make screen review easier.
- Added the Leaf v3.2.8 hardware schematic.
- Faster firmware builds through reusable build caches; standard builds and factory build selection use release configurations, including v3.2.8.
- Expanded developer guidance for storage recovery, diagnostics and firmware releases, including candidate runtime reports and coordinated website updates.

### C. Bug fixes and stability

No changes in this category.
