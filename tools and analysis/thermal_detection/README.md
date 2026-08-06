# IGC Thermal Detection Replay

This folder contains an exploratory GPS/IGC-pressure altitude thermal detector and replay tool.

The generator parses IGC `B` records, preserves GPS altitude, IGC pressure altitude, and Leaf's optional logged track/climb extensions when present, skips invalid zero GPS-altitude samples, projects fixes into a local meter grid, and detects thermal candidates using:

- net selected-source altitude gain over a rolling window
- the longest continuous same-direction arc over the same window, preferring Leaf's logged `TRT`
  track-angle extension when present and otherwise using a 5-second smoothed GPS bearing estimate
  with direction-reversal hysteresis so opposite circles do not cancel and short S-turns do not combine
- a persistent episode-entry snapshot plus minimum entry-to-peak duration and total gain before
  saving a thermal

Build the replay from an IGC file:

```powershell
& 'C:\Users\oxoth\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' 'tools and analysis\thermal_detection\build_igc_replay.py' 'C:\path\to\flight.igc'
```

Open `igc_thermal_replay.html` in a browser to review detector state, candidate periods, and saved thermal locations during playback.

## Terrain Data

Terrain data is optional and is fetched by the Python generator, not by the browser. This avoids browser CORS/local-script restrictions and keeps the generated `igc_thermal_replay.html` self-contained after generation.

To regenerate the default replay with terrain:

```powershell
& 'C:\Users\oxoth\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' 'tools and analysis\thermal_detection\build_igc_replay.py' 'C:\path\to\flight.igc' --terrain
```

To write a separate HTML file instead of replacing the checked-in replay page:

```powershell
& 'C:\Users\oxoth\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' 'tools and analysis\thermal_detection\build_igc_replay.py' 'C:\path\to\flight.igc' --terrain --out 'C:\path\to\flight_replay.html'
```

`--terrain` embeds two terrain products:

- `groundAlt` on each replay fix, interpolated from terrain samples every 10 seconds by default. This drives the profile ground line and current AGL readout.
- `terrainMesh`, a coarse map-floor elevation grid. This drives the shaded 3D ground/mountain surface under the flight track.

Useful terrain options:

- `--terrain-dataset srtm90m` selects the Open Topo Data dataset. `srtm90m` is the default.
- `--terrain-sample-step-s 10` controls how often the flight path is sampled for profile/AGL terrain.
- `--terrain-grid-resolution-m 500` controls the 3D map mesh spacing. Use `250` or `200` for more detail, at the cost of more API calls and a larger generated HTML file.
- `--terrain-batch-size 100` controls terrain API batch size. The builder clamps this to the public API's 100-location maximum.
- `--terrain-api-base https://api.opentopodata.org/v1` can point at another Open Topo Data-compatible endpoint.

The default public Open Topo Data service currently allows 100 locations per request, 1 request per second, and 1000 requests per day. The builder spaces repeated requests to respect the 1 request/second limit. For the default June 12 test IGC, `--terrain` with the 500 m mesh took about 15 seconds and produced a 30 x 14 mesh.

For a quick terrain-enabled rebuild at coarser/faster map detail:

```powershell
& 'C:\Users\oxoth\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' 'tools and analysis\thermal_detection\build_igc_replay.py' 'C:\path\to\flight.igc' --terrain --terrain-grid-resolution-m 500
```

For a more detailed map floor:

```powershell
& 'C:\Users\oxoth\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' 'tools and analysis\thermal_detection\build_igc_replay.py' 'C:\path\to\flight.igc' --terrain --terrain-grid-resolution-m 250
```

The replay page includes:

- play/pause, scrub, and playback-speed controls
- a local IGC file picker for trying other flight logs without rebuilding
- a "How this works" dialog explaining candidate episodes, save thresholds, and card colors
- an altitude tape scaled to the selected flight's logged altitude sources
- a vario bar showing Leaf's logged `VAR` climb-rate extension when present, otherwise the selected-source 3-5 second average climb/sink rate, across a +/-7 m/s range
- an altitude profile along the bottom with candidate fixes and labeled saved-thermal timestamps
- an optional terrain/ground profile and current AGL readout when generated with `--terrain`
- an optional coarse 3D terrain mesh under the flight track when generated with `--terrain`
- GPS and IGC pressure-altitude profile traces whenever both are available
- mutually exclusive detector-source checkboxes for GPS altitude or IGC pressure altitude
- realtime thermal quality cards with gain, average climb, duration, and total turns
- realtime rejected-candidate cards showing which save thresholds failed
- mouse-wheel map zoom, click-drag map panning, and a Reset Map button for inspecting saved thermal locations
- in-map Follow and Terrain checkboxes; Terrain appears only when the replay has embedded terrain mesh data
- live algorithm sliders/value boxes for tuning thresholds
- realtime replay semantics: saved thermals appear only after the simulated detector reaches the save point
- long-episode semantics: the detector snapshots the bottom of the first qualifying window and
  evaluates through the episode peak, so the rolling-window length does not cap slow thermal climbs
- pause-only detector-source and parameter editing; changed settings require restarting the replay so one run always uses one consistent detector state
