# IGC Thermal Detection Replay

This folder contains an exploratory GPS/IGC-pressure altitude thermal detector and replay tool.

The generator parses IGC `B` records, preserves both GPS and IGC pressure-altitude fields when present, skips invalid zero GPS-altitude samples, projects fixes into a local meter grid, and detects thermal candidates using:

- net selected-source altitude gain over a rolling window
- the longest continuous same-direction arc over the same window, using a 5-second smoothed GPS
  bearing and direction-reversal hysteresis so opposite circles do not cancel and short S-turns do
  not combine
- a persistent episode-entry snapshot plus minimum entry-to-peak duration and total gain before
  saving a thermal

Build the replay from an IGC file:

```powershell
& 'C:\Users\oxoth\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' 'tools and analysis\thermal_detection\build_igc_replay.py' 'C:\path\to\flight.igc'
```

Open `igc_thermal_replay.html` in a browser to review detector state, candidate periods, and saved thermal locations during playback.

The replay page includes:

- play/pause, scrub, and playback-speed controls
- a local IGC file picker for trying other flight logs without rebuilding
- a "How this works" dialog explaining candidate episodes, save thresholds, and card colors
- an altitude tape scaled to the selected flight's logged altitude sources
- a vario bar showing the selected-source 3-5 second average climb/sink rate across a +/-7 m/s range
- an altitude profile along the bottom with candidate fixes and labeled saved-thermal timestamps
- GPS and IGC pressure-altitude profile traces whenever both are available
- mutually exclusive detector-source checkboxes for GPS altitude or IGC pressure altitude
- realtime thermal quality cards with gain, average climb, duration, and total turns
- realtime rejected-candidate cards showing which save thresholds failed
- mouse-wheel map zoom, click-drag map panning, and a Reset Map button for inspecting saved thermal locations
- live algorithm sliders/value boxes for tuning thresholds
- realtime replay semantics: saved thermals appear only after the simulated detector reaches the save point
- long-episode semantics: the detector snapshots the bottom of the first qualifying window and
  evaluates through the episode peak, so the rolling-window length does not cap slow thermal climbs
- pause-only detector-source and parameter editing; changed settings require restarting the replay so one run always uses one consistent detector state
