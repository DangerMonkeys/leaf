from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path


DEFAULTS = {
    "window_s": 75,
    "min_gain_m": 25,
    "min_turn_deg": 90,
    "min_duration_s": 25,
    "min_save_gain_m": 35,
    "merge_radius_m": 300,
    "bearing_lookback_s": 8,
    "min_bearing_distance_m": 18,
    "stale_tolerance_s": 8,
    "vario_avg_s": 5,
}


@dataclass
class Fix:
    t: int
    lat: float
    lon: float
    alt: int
    pressure_alt: int | None


def parse_coord(value: str, hemi: str, deg_digits: int) -> float:
    deg = int(value[:deg_digits])
    minutes = float(value[deg_digits:]) / 1000.0
    result = deg + minutes / 60.0
    return -result if hemi in ("S", "W") else result


def parse_b_record(line: str) -> Fix | None:
    if len(line) < 35 or not line.startswith("B"):
        return None
    time_s = int(line[1:3]) * 3600 + int(line[3:5]) * 60 + int(line[5:7])
    pressure_alt = int(line[25:30])
    gps_alt = int(line[30:35])
    if gps_alt <= 0:
        return None
    lat = parse_coord(line[7:14], line[14], 2)
    lon = parse_coord(line[15:23], line[23], 3)
    return Fix(time_s, lat, lon, gps_alt, pressure_alt if pressure_alt > 0 else None)


def unwrap_times(fixes: list[Fix]) -> None:
    day_offset = 0
    previous = fixes[0].t
    for fix in fixes:
        if fix.t + day_offset < previous - 12 * 3600:
            day_offset += 24 * 3600
        fix.t += day_offset
        previous = fix.t
    start = fixes[0].t
    for fix in fixes:
        fix.t -= start


def load_igc(path: Path) -> list[Fix]:
    fixes = [fix for line in path.read_text(errors="replace").splitlines() if (fix := parse_b_record(line))]
    if not fixes:
        raise ValueError(f"No valid B records found in {path}")
    unwrap_times(fixes)
    return fixes


def build_payload(igc_path: Path) -> dict:
    fixes = load_igc(igc_path)
    return {
        "source": igc_path.name,
        "defaults": DEFAULTS,
        "fixes": [
            {"t": f.t, "lat": round(f.lat, 7), "lon": round(f.lon, 7), "alt": f.alt, "pressureAlt": f.pressure_alt}
            for f in fixes
        ],
    }


def render_html(payload: dict) -> str:
    data = json.dumps(payload, separators=(",", ":"))
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Leaf GPS Thermal Detector Replay</title>
  <style>
    :root {{
      color-scheme: dark;
      --bg: #101010;
      --fg: #f4f4f4;
      --muted: #a7a7a7;
      --panel: #1b1b1b;
      --border: #3e3e3e;
      --grid: #303030;
      --track: #d8d8d8;
      --pressure: #c9a0ff;
      --candidate: #84c8ff;
      --thermal: #ffd166;
      --merged: #9cff57;
      --failed: #ff8c78;
      --sink: #ff8c78;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      background: var(--bg);
      color: var(--fg);
      font-family: system-ui, -apple-system, Segoe UI, sans-serif;
    }}
    main {{
      max-width: none;
      margin: 0;
      padding: 18px;
    }}
    h1 {{
      margin: 0;
      font-size: 22px;
      font-weight: 600;
    }}
    .controls {{
      display: flex;
      flex-wrap: wrap;
      gap: 10px 14px;
      align-items: center;
    }}
    .layout {{
      display: grid;
      grid-template-columns: minmax(520px, 1fr) 168px 184px 210px 430px;
      gap: 16px;
      align-items: start;
    }}
    .map-column {{
      display: grid;
      gap: 12px;
      min-width: 0;
    }}
    .panel {{
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 12px;
    }}
    canvas {{
      width: 100%;
      height: min(76vh, calc(100vh - 150px));
      min-height: 640px;
      background: #050505;
      border: 1px solid var(--border);
      border-radius: 8px;
      cursor: grab;
      touch-action: none;
    }}
    canvas.is-panning {{
      cursor: grabbing;
    }}
    .replay-stack {{
      width: 100%;
      min-width: 0;
    }}
    .speed-control input[type="range"] {{
      width: 300px;
      max-width: min(300px, 52vw);
    }}
    .time-scrub {{
      display: grid;
      gap: 4px;
      margin-top: 8px;
      margin-left: var(--profile-left, 90px);
      width: var(--profile-width, calc(100% - 140px));
      max-width: calc(100% - var(--profile-left, 90px));
    }}
    .time-scrub input[type="range"] {{
      width: 100%;
    }}
    button, input::file-selector-button {{
      font: inherit;
      color: var(--fg);
      background: #2b2b2b;
      border: 1px solid #666;
      border-radius: 6px;
      padding: 7px 12px;
    }}
    input[type="range"] {{ width: 150px; }}
    input[type="number"] {{
      width: 72px;
      color: var(--fg);
      background: #111;
      border: 1px solid #555;
      border-radius: 5px;
      padding: 5px 6px;
    }}
    label {{
      color: var(--muted);
      font-size: 13px;
      white-space: nowrap;
    }}
    .check {{
      display: inline-flex;
      align-items: center;
      gap: 6px;
    }}
    .source-stack {{
      display: grid;
      gap: 4px;
    }}
    .gps-source {{
      color: var(--track);
    }}
    .pressure-source {{
      color: var(--pressure);
    }}
    .param {{
      display: grid;
      grid-template-columns: 135px minmax(160px, 1fr) 72px;
      gap: 4px 10px;
      align-items: center;
      width: 100%;
    }}
    .param-grid {{
      display: grid;
      gap: 12px;
    }}
    .param-group {{
      display: grid;
      gap: 9px;
      padding: 10px;
      border: 1px solid var(--border);
      border-radius: 8px;
      background: #171717;
    }}
    .param-group h3 {{
      margin: 0;
      font-size: 13px;
      font-weight: 600;
      color: var(--fg);
    }}
    .param input[type="range"] {{
      width: 100%;
      min-width: 0;
    }}
    .param input[type="number"] {{
      justify-self: end;
    }}
    .param-desc {{
      color: #8f8f8f;
      font-size: 12px;
      line-height: 1.25;
      grid-column: 1 / -1;
      white-space: normal;
    }}
    dl {{
      display: grid;
      grid-template-columns: minmax(140px, 1fr) minmax(120px, max-content);
      gap: 7px 12px;
      margin: 0;
      font-variant-numeric: tabular-nums;
    }}
    dt {{
      color: var(--muted);
      white-space: nowrap;
    }}
    dd {{
      margin: 0;
      text-align: right;
      white-space: nowrap;
    }}
    .candidate {{ color: var(--candidate); }}
    .ok {{ color: var(--thermal); }}
    .thermal-list {{
      display: grid;
      gap: 8px;
      font-size: 13px;
      font-variant-numeric: tabular-nums;
    }}
    .thermal-column {{
      display: grid;
      gap: 10px;
      align-content: start;
    }}
    .thermal-column h2 {{
      margin: 0;
      color: var(--muted);
      font-size: 13px;
      font-weight: 600;
    }}
    .thermal-card, .candidate-card, .merged-card {{
      display: grid;
      gap: 4px;
      padding: 9px;
      border: 1px solid #57503a;
      border-radius: 7px;
      background: #201d12;
      cursor: pointer;
    }}
    .candidate-card {{
      border-color: #4b5d6d;
      background: #111c24;
    }}
    .merged-card {{
      border-color: #527d39;
      background: #13200f;
    }}
    .thermal-card strong, .candidate-card strong, .merged-card strong {{
      color: var(--thermal);
      font-size: 15px;
      line-height: 1;
    }}
    .candidate-card strong {{
      color: var(--candidate);
    }}
    .merged-card strong {{
      color: var(--merged);
    }}
    .thermal-card div, .candidate-card div, .merged-card div {{
      display: flex;
      justify-content: space-between;
      gap: 8px;
      white-space: nowrap;
    }}
    .thermal-card span, .candidate-card span, .merged-card span {{
      color: var(--muted);
    }}
    .thermal-card.is-selected, .candidate-card.is-selected, .merged-card.is-selected {{
      border-width: 3px;
      padding: 7px;
    }}
    .thermal-card.is-selected {{
      border-color: var(--thermal);
    }}
    .candidate-card.is-selected {{
      border-color: var(--candidate);
    }}
    .merged-card.is-selected {{
      border-color: var(--merged);
    }}
    .candidate-card .pass {{
      color: var(--candidate);
    }}
    .candidate-card .fail {{
      color: var(--failed);
    }}
    .small {{
      color: var(--muted);
      font-size: 13px;
      line-height: 1.35;
      margin: 12px 0 0;
    }}
    .param-tools {{
      display: flex;
      justify-content: flex-end;
      margin: 14px 0 10px;
    }}
    dialog {{
      max-width: 620px;
      color: var(--fg);
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 18px;
    }}
    dialog::backdrop {{
      background: rgba(0, 0, 0, 0.65);
    }}
    .help-body {{
      display: grid;
      gap: 10px;
      color: var(--muted);
      font-size: 14px;
      line-height: 1.45;
    }}
    .help-body h2 {{
      margin: 0;
      color: var(--fg);
      font-size: 18px;
    }}
    .help-body ol, .help-body ul {{
      margin: 0;
      padding-left: 22px;
    }}
    .help-body li {{
      margin: 5px 0;
    }}
    @media (max-width: 940px) {{
      .layout {{ grid-template-columns: 1fr; }}
      .param {{ grid-template-columns: 1fr; }}
      input[type="range"] {{ width: 100%; }}
      .speed-control input[type="range"] {{ width: min(100%, 300px); }}
      canvas {{
        height: 72vh;
        min-height: 520px;
      }}
    }}
  </style>
</head>
<body>
<main>
  <div class="layout">
    <div class="map-column">
      <h1>Leaf GPS Thermal Detector Replay</h1>
      <section class="panel">
        <div class="controls">
          <button id="playPause" type="button">Play</button>
          <label class="speed-control">Speed <input id="speed" type="range" min="1" max="1000" step="1" value="15"> <span id="speedLabel">15x</span></label>
          <label>Load IGC <input id="fileInput" type="file" accept=".igc,text/plain"></label>
          <button id="restartWithParams" type="button" disabled>Restart with Parameters</button>
          <button id="resetMapView" type="button">Reset Map</button>
          <button id="resetDefaults" type="button">Reset Defaults</button>
          <div class="source-stack" aria-label="Detector altitude source">
            <label class="check gps-source"><input id="showGpsAlt" type="checkbox" checked> Detect from GPS altitude</label>
            <label class="check pressure-source"><input id="showPressureAlt" type="checkbox"> Detect from IGC pressure altitude</label>
          </div>
        </div>
        <p class="small" id="runState">Realtime replay: saved thermals appear only after the simulated detector saves them.</p>
      </section>
      <div class="replay-stack" id="replayStack">
        <canvas id="replay" width="930" height="720" aria-label="Flight replay 3D map with altitude tape, vario bar, and altitude profile"></canvas>
        <label class="time-scrub" for="scrub">Time
          <input id="scrub" type="range" min="0" max="1000" value="0">
        </label>
      </div>
    </div>
    <section class="panel thermal-column" aria-label="Detected thermal quality cards">
      <h2>Thermals</h2>
      <div class="thermal-list" id="thermalList"></div>
    </section>
    <section class="panel thermal-column" aria-label="Rejected candidate cards">
      <h2>Candidates</h2>
      <div class="thermal-list" id="candidateList"></div>
    </section>
    <section class="panel thermal-column" aria-label="Merged thermal cards">
      <h2>Merged</h2>
      <div class="thermal-list" id="mergedList"></div>
    </section>
    <aside class="panel">
      <dl>
        <dt>Source</dt><dd id="sourceName">--</dd>
        <dt>Time</dt><dd id="time">--</dd>
        <dt>Altitude</dt><dd id="alt">--</dd>
        <dt>Vario avg</dt><dd id="vario">--</dd>
        <dt>Window gain</dt><dd id="gain">--</dd>
        <dt>Window turn</dt><dd id="turn">--</dd>
        <dt>Detector</dt><dd id="detector">--</dd>
        <dt>Saved thermals</dt><dd id="saved">--</dd>
      </dl>
      <div class="param-tools">
        <button id="howThisWorks" type="button">How this works</button>
      </div>
      <div class="param-grid" id="params"></div>
    </aside>
  </div>
  <dialog id="howDialog">
    <div class="help-body">
      <h2>How Thermal Detection Works</h2>
      <p id="helpRunSummary"></p>
      <ol>
        <li>Each flight log point is checked against the moving time window. A point becomes a candidate point only when both Candidate gain and Candidate turn are met inside that window.</li>
        <li>A candidate episode starts when candidate points begin. It stays open while candidate points continue, including short non-candidate gaps up to Candidate gap seconds.</li>
        <li>The episode ends after the detector has gone longer than Candidate gap seconds since the last candidate point.</li>
        <li>When the episode ends, it is evaluated against Save duration and Save gain. Passing episodes become saved thermals. Failed episodes become candidate cards.</li>
      </ol>
      <ul>
        <li>Thermal cards show saved thermal episodes: gain, duration, average climb, and total turns.</li>
        <li>Candidate and thermal numbers share one episode sequence. For example, C3 becomes T3 if parameter changes make that same flight segment pass the save checks.</li>
        <li>Candidate cards show rejected episodes. Failed save checks are red; checks that passed are blue.</li>
        <li>Merged cards are derived from saved thermals within Merge radius. Source thermal cards keep their original T numbers.</li>
        <li>The 3D map, altitude tape, profile, vario bar, Window gain, candidate dots, and saved thermal metrics use the selected detector altitude source.</li>
        <li>When GPS and IGC pressure altitude are both available, selecting either source changes both the analysis and the displayed altitude trace.</li>
      </ul>
      <button id="closeHowDialog" type="button">Close</button>
    </div>
  </dialog>
</main>
<script>
const embedded = {data};
const defaultParams = embedded.defaults;
const canvas = document.getElementById("replay");
const ctx = canvas.getContext("2d");
const playPause = document.getElementById("playPause");
const speed = document.getElementById("speed");
const speedLabel = document.getElementById("speedLabel");
const scrub = document.getElementById("scrub");
const replayStack = document.getElementById("replayStack");
const fileInput = document.getElementById("fileInput");
const restartWithParams = document.getElementById("restartWithParams");
const resetMapView = document.getElementById("resetMapView");
const resetDefaults = document.getElementById("resetDefaults");
const showGpsAlt = document.getElementById("showGpsAlt");
const showPressureAlt = document.getElementById("showPressureAlt");
const howThisWorks = document.getElementById("howThisWorks");
const howDialog = document.getElementById("howDialog");
const closeHowDialog = document.getElementById("closeHowDialog");
const helpRunSummary = document.getElementById("helpRunSummary");
const paramSpecs = [
  ["window_s", "Window", 20, 180, 5, "sec", "How far back in time, in seconds, the detector looks when calculating altitude gain and accumulated turn for the current fix."],
  ["min_gain_m", "Candidate gain", 5, 120, 5, "m", "Minimum net altitude gain, in meters, required within the time window before the current fix can count as a thermal candidate. Uses the selected detector altitude source."],
  ["min_turn_deg", "Candidate turn", 20, 540, 10, "deg", "Minimum net heading change, in degrees, required within the time window before the current fix can count as circling. 360 deg is one full turn."],
  ["min_duration_s", "Save duration", 5, 180, 5, "sec", "Minimum candidate episode length, in seconds, required before the detector is allowed to save the episode as a thermal."],
  ["min_save_gain_m", "Save gain", 10, 180, 5, "m", "Minimum total altitude gain, in meters, across a candidate episode before saving a thermal marker. Uses the selected detector altitude source."],
  ["merge_radius_m", "Merge radius", 50, 1000, 25, "m", "Maximum horizontal distance, in meters, between saved thermal centers before the replay shows them as a derived merged thermal. This does not change the original T numbers."],
  ["bearing_lookback_s", "Bearing smooth", 2, 20, 1, "sec", "How far back in time, in seconds, each GPS bearing sample looks when estimating track direction. Larger values smooth heading noise but react more slowly."],
  ["min_bearing_distance_m", "Bearing distance", 2, 60, 1, "m", "Minimum horizontal movement, in meters, required before a bearing sample is accepted. Higher values reject more GPS jitter at low speed."],
  ["stale_tolerance_s", "Candidate gap", 0, 30, 1, "sec", "Maximum gap, in seconds, allowed between candidate fixes before the current thermal episode is ended and evaluated for saving."],
  ["vario_avg_s", "Vario average", 3, 10, 1, "sec", "Averaging period, in seconds, for the replay vario bar and displayed vario value. This does not change candidate detection or saved thermals."]
];
const paramByKey = new Map(paramSpecs.map(spec => [spec[0], spec]));
const paramGroups = [
  ["Candidate detection", ["window_s", "min_gain_m", "min_turn_deg", "stale_tolerance_s"]],
  ["Pass/fail criteria", ["min_duration_s", "min_save_gain_m", "merge_radius_m"]],
  ["Smoothing / averaging", ["bearing_lookback_s", "min_bearing_distance_m", "vario_avg_s"]]
];
let params = {{...defaultParams}};
let pendingParams = {{...defaultParams}};
let rawFixes = embedded.fixes;
let processed = [];
let bounds = null;
let sourceName = embedded.source;
let playing = false;
let paramsDirty = false;
let altitudeSource = preferredAltitudeSource(rawFixes);
let pendingAltitudeSource = altitudeSource;
let mapView = null;
let isPanning = false;
let mapDragMode = null;
let lastPan = null;
let selectedCard = null;
let followCurrentFix = false;
let followControlRect = null;
let currentFrameItems = {{saved: [], failed: [], merged: []}};
let replayT = 0;
let lastNow = performance.now();

function parseCoord(value, hemi, degDigits) {{
  const deg = Number(value.slice(0, degDigits));
  const minutes = Number(value.slice(degDigits)) / 1000;
  const result = deg + minutes / 60;
  return hemi === "S" || hemi === "W" ? -result : result;
}}

function parseIgc(text) {{
  const fixes = [];
  for (const line of text.split(/\\r?\\n/)) {{
    if (!line.startsWith("B") || line.length < 35) continue;
    const pressureAlt = Number(line.slice(25, 30));
    const alt = Number(line.slice(30, 35));
    if (!Number.isFinite(alt) || alt <= 0) continue;
    fixes.push({{
      t: Number(line.slice(1, 3)) * 3600 + Number(line.slice(3, 5)) * 60 + Number(line.slice(5, 7)),
      lat: parseCoord(line.slice(7, 14), line[14], 2),
      lon: parseCoord(line.slice(15, 23), line[23], 3),
      alt,
      pressureAlt: Number.isFinite(pressureAlt) && pressureAlt > 0 ? pressureAlt : null
    }});
  }}
  if (!fixes.length) throw new Error("No valid B records found.");
  unwrapTimes(fixes);
  return fixes;
}}

function unwrapTimes(fixes) {{
  let dayOffset = 0;
  let previous = fixes[0].t;
  for (const fix of fixes) {{
    if (fix.t + dayOffset < previous - 12 * 3600) dayOffset += 24 * 3600;
    fix.t += dayOffset;
    previous = fix.t;
  }}
  const start = fixes[0].t;
  for (const fix of fixes) fix.t -= start;
}}

function project(fixes) {{
  const lat0 = avg(fixes.map(f => f.lat)) * Math.PI / 180;
  const lon0 = avg(fixes.map(f => f.lon)) * Math.PI / 180;
  const radiusM = 6371000;
  for (const fix of fixes) {{
    const lat = fix.lat * Math.PI / 180;
    const lon = fix.lon * Math.PI / 180;
    fix.x = (lon - lon0) * Math.cos(lat0) * radiusM;
    fix.y = (lat - lat0) * radiusM;
  }}
}}

function avg(values) {{
  return values.reduce((a, b) => a + b, 0) / values.length;
}}

function angleDeg(dx, dy) {{
  return (Math.atan2(dx, dy) * 180 / Math.PI + 360) % 360;
}}

function angleDelta(a, b) {{
  return ((a - b + 540) % 360) - 180;
}}

function bearing(a, b) {{
  return angleDeg(b.x - a.x, b.y - a.y);
}}

function priorIndex(fixes, i, secondsBack) {{
  const target = fixes[i].t - secondsBack;
  let j = i;
  while (j > 0 && fixes[j].t > target) j--;
  return j;
}}

function hasPressureAltitude(fixes) {{
  return fixes.some(f => Number.isFinite(f.pressureAlt));
}}

function preferredAltitudeSource(fixes) {{
  return hasPressureAltitude(fixes) ? "pressure" : "gps";
}}

function altitudeFor(fix, source) {{
  return source === "pressure" && Number.isFinite(fix.pressureAlt) ? fix.pressureAlt : fix.alt;
}}

function altitudeSourceLabel() {{
  return altitudeSource === "pressure" ? "IGC pressure altitude" : "GPS altitude";
}}

function ensureAltitudeControls() {{
  const pressureAvailable = hasPressureAltitude(rawFixes);
  if (!pressureAvailable && pendingAltitudeSource === "pressure") pendingAltitudeSource = "gps";
  if (!pressureAvailable && altitudeSource === "pressure") altitudeSource = "gps";
  showPressureAlt.disabled = !pressureAvailable || playing;
  showGpsAlt.disabled = playing;
  showGpsAlt.checked = pendingAltitudeSource === "gps";
  showPressureAlt.checked = pendingAltitudeSource === "pressure" && pressureAvailable;
}}

function recomputeMetrics() {{
  processed = rawFixes.map(f => ({{...f}}));
  project(processed);
  ensureAltitudeControls();
  for (let i = 0; i < processed.length; i++) {{
    const fix = processed[i];
    fix.detectorAlt = altitudeFor(fix, altitudeSource);
    const bearingStart = priorIndex(processed, i, params.bearing_lookback_s);
    fix.bearing = null;
    if (bearingStart < i) {{
      const dx = fix.x - processed[bearingStart].x;
      const dy = fix.y - processed[bearingStart].y;
      if (Math.hypot(dx, dy) >= params.min_bearing_distance_m) {{
        fix.bearing = bearing(processed[bearingStart], fix);
      }}
    }}
    const varioStart = priorIndex(processed, i, params.vario_avg_s);
    const climbStart = priorIndex(processed, i, 30);
    const varioDt = Math.max(1, fix.t - processed[varioStart].t);
    fix.vario = (fix.detectorAlt - processed[varioStart].detectorAlt) / varioDt;
    fix.climb30 = (fix.detectorAlt - processed[climbStart].detectorAlt) / Math.max(1, fix.t - processed[climbStart].t);
    const start = priorIndex(processed, i, params.window_s);
    const window = processed.slice(start, i + 1);
    fix.windowDuration = window[window.length - 1].t - window[0].t;
    fix.windowGain = fix.detectorAlt - window[0].detectorAlt;
    let signedTurn = 0;
    let lastBearing = null;
    for (const point of window) {{
      if (point.bearing === null) continue;
      if (lastBearing !== null) signedTurn += angleDelta(point.bearing, lastBearing);
      lastBearing = point.bearing;
    }}
    fix.windowTurn = Math.abs(signedTurn);
    fix.candidate = fix.windowGain >= params.min_gain_m && fix.windowTurn >= params.min_turn_deg;
    fix.thermal = null;
  }}
  bounds = buildBounds(processed);
  resetMapCamera();
  replayT = Math.min(replayT, processed[processed.length - 1].t);
  updateSummary();
}}

function detectThermalsThrough(fixes, endIndex) {{
  const saved = [];
  const failed = [];
  let active = [];
  let lastCandidateT = null;
  for (let i = 0; i <= endIndex; i++) {{
    const fix = fixes[i];
    if (fix.candidate) {{
      active.push(fix);
      lastCandidateT = fix.t;
      continue;
    }}
    if (active.length && lastCandidateT !== null && fix.t - lastCandidateT <= params.stale_tolerance_s) {{
      active.push(fix);
      continue;
    }}
    if (active.length) {{
      const episodeId = saved.length + failed.length + 1;
      evaluateEpisode(active, saved, failed, episodeId);
      active = [];
      lastCandidateT = null;
    }}
  }}
  return {{saved, failed}};
}}

function evaluateEpisode(points, saved, failed, episodeId) {{
  const duration = points[points.length - 1].t - points[0].t;
  const alts = points.map(p => p.detectorAlt);
  const gain = Math.max(...alts) - Math.min(...alts);
  const turnDeg = episodeTurnDeg(points);
  const center = weightedCenter(points);
  const episode = {{
    id: episodeId,
    x: center.x,
    y: center.y,
    z: center.z,
    lat: center.lat,
    lon: center.lon,
    start_s: points[0].t,
    end_s: points[points.length - 1].t,
    duration_s: duration,
    gain_m: gain,
    avg_climb_mps: gain / Math.max(1, duration),
    turns: turnDeg / 360,
    failed_duration: duration < params.min_duration_s,
    failed_gain: gain < params.min_save_gain_m
  }};
  if (episode.failed_duration || episode.failed_gain) {{
    failed.push(episode);
    return;
  }}
  const next = {{
    id: episode.id,
    x: center.x,
    y: center.y,
    z: center.z,
    lat: center.lat,
    lon: center.lon,
    start_s: episode.start_s,
    end_s: episode.end_s,
    duration_s: episode.duration_s,
    gain_m: episode.gain_m,
    avg_climb_mps: episode.avg_climb_mps,
    turns: episode.turns,
    max_climb_mps: Math.max(...points.map(p => p.climb30))
  }};
  saved.push(next);
  for (const point of points) point.thermal = next.id;
}}

function thermalDistance(a, b) {{
  return Math.hypot(a.x - b.x, a.y - b.y);
}}

function mergedFromSources(sources) {{
  const ordered = [...sources].sort((a, b) => a.id - b.id);
  const activeTime = ordered.reduce((sum, th) => sum + th.duration_s, 0);
  const accruedGain = ordered.reduce((sum, th) => sum + th.gain_m, 0);
  const turns = ordered.reduce((sum, th) => sum + th.turns, 0);
  const weightTotal = Math.max(1, activeTime);
  const weighted = key => ordered.reduce((sum, th) => sum + th[key] * Math.max(1, th.duration_s), 0) / weightTotal;
  return {{
    sources: ordered,
    sourceIds: ordered.map(th => th.id),
    key: ordered.map(th => th.id).join("+"),
    label: ordered.map(th => `T${{th.id}}`).join(" + "),
    x: weighted("x"),
    y: weighted("y"),
    z: weighted("z"),
    start_s: Math.min(...ordered.map(th => th.start_s)),
    end_s: Math.max(...ordered.map(th => th.end_s)),
    gain_m: accruedGain,
    duration_s: activeTime,
    avg_climb_mps: accruedGain / Math.max(1, activeTime),
    turns
  }};
}}

function buildMergedThermals(saved) {{
  const groups = [];
  for (const th of saved) {{
    const matches = groups.filter(group =>
      group.sources.some(source => thermalDistance(th, source) <= params.merge_radius_m)
    );
    if (!matches.length) {{
      groups.push(mergedFromSources([th]));
      continue;
    }}
    const nextSources = [th];
    for (const group of matches) nextSources.push(...group.sources);
    for (const group of matches) {{
      const index = groups.indexOf(group);
      if (index >= 0) groups.splice(index, 1);
    }}
    groups.push(mergedFromSources(nextSources));
  }}
  return groups
    .filter(group => group.sources.length > 1)
    .sort((a, b) => a.sourceIds[0] - b.sourceIds[0]);
}}

function compactMergedLabel(merged) {{
  return merged.sourceIds.map(id => `T${{id}}`).join("+");
}}

function selectedClass(kind, id) {{
  return isSelected(kind, id) ? " is-selected" : "";
}}

function isSelected(kind, id) {{
  return selectedCard && selectedCard.kind === kind && selectedCard.id === String(id);
}}

function itemPosition(item) {{
  return item ? {{x: item.x, y: item.y, z: item.z ?? item.detectorAlt ?? bounds.minAlt}} : null;
}}

function centerMapOnPosition(position) {{
  if (!position) return;
  if (!mapView) resetMapCamera();
  mapView.cx = position.x;
  mapView.cy = position.y;
  mapView.cz = Math.max(0, (position.z ?? bounds.minAlt) - bounds.minAlt);
  mapView.panX = 0;
  mapView.panY = 0;
}}

function smoothedFollowPosition(index) {{
  if (!processed.length) return null;
  const windowCount = Math.min(100, processed.length);
  const end = Math.max(windowCount - 1, index);
  const start = Math.max(0, end - windowCount + 1);
  let total = 0;
  const acc = {{x: 0, y: 0, z: 0}};
  for (let i = start; i <= end && i < processed.length; i += 1) {{
    const point = processed[i];
    acc.x += point.x;
    acc.y += point.y;
    acc.z += point.detectorAlt;
    total += 1;
  }}
  if (!total) return itemPosition(processed[index]);
  return {{x: acc.x / total, y: acc.y / total, z: acc.z / total}};
}}

function selectCard(kind, id) {{
  const key = String(id);
  let item = null;
  if (kind === "thermal") item = currentFrameItems.saved.find(th => String(th.id) === key);
  else if (kind === "candidate") item = currentFrameItems.failed.find(candidate => String(candidate.id) === key);
  else if (kind === "merged") item = currentFrameItems.merged.find(merge => merge.key === key);
  const position = itemPosition(item);
  if (!position) return;
  selectedCard = {{kind, id: key}};
  centerMapOnPosition(position);
  draw();
}}

function handleCardListClick(event) {{
  if (event.button !== undefined && event.button !== 0) return;
  const card = event.target.closest("[data-select-kind]");
  if (!card) return;
  event.preventDefault();
  selectCard(card.dataset.selectKind, card.dataset.selectId);
}}

function episodeTurnDeg(points) {{
  let total = 0;
  let lastBearing = null;
  for (const point of points) {{
    if (point.bearing === null) continue;
    if (lastBearing !== null) total += Math.abs(angleDelta(point.bearing, lastBearing));
    lastBearing = point.bearing;
  }}
  return total;
}}

function weightedCenter(points) {{
  let total = 0;
  const acc = {{x: 0, y: 0, z: 0, lat: 0, lon: 0}};
  for (const p of points) {{
    const w = Math.max(0.2, Math.min(5, p.climb30) + 0.5);
    total += w;
    acc.x += p.x * w;
    acc.y += p.y * w;
    acc.z += p.detectorAlt * w;
    acc.lat += p.lat * w;
    acc.lon += p.lon * w;
  }}
  return {{x: acc.x / total, y: acc.y / total, z: acc.z / total, lat: acc.lat / total, lon: acc.lon / total}};
}}

function buildBounds(fixes) {{
  const xs = fixes.map(f => f.x);
  const ys = fixes.map(f => f.y);
  const alts = fixes.map(f => f.detectorAlt).filter(v => Number.isFinite(v));
  return {{
    minX: Math.min(...xs), maxX: Math.max(...xs),
    minY: Math.min(...ys), maxY: Math.max(...ys),
    minAlt: Math.min(...alts), maxAlt: Math.max(...alts),
    duration: fixes[fixes.length - 1].t
  }};
}}

function visibleAltitudeRange(fixes) {{
  const alts = fixes.map(f => f.detectorAlt).filter(v => Number.isFinite(v));
  return {{min: Math.min(...alts), max: Math.max(...alts)}};
}}

function resetMapCamera() {{
  if (!bounds) return;
  mapView = {{
    cx: (bounds.maxX + bounds.minX) / 2,
    cy: (bounds.maxY + bounds.minY) / 2,
    zoom: 1,
    yaw: -0.7,
    pitch: 0.9,
    cz: 0,
    panX: 0,
    panY: 0
  }};
}}

function mapLayout() {{
  const W = canvas.width;
  const H = canvas.height;
  const outerPad = 24;
  const topPad = 34;
  const sideGap = 20;
  const tapeW = 68;
  const varioW = 58;
  const profileGap = 40;
  const profileH = Math.max(100, Math.min(160, H * 0.18));
  const mapH = Math.max(260, H - topPad - profileGap - profileH - outerPad);
  const mapW = Math.max(320, W - outerPad * 2 - tapeW - varioW - sideGap * 2);
  const tape = {{x: outerPad, y: topPad, w: tapeW, h: mapH}};
  const map = {{x: tape.x + tape.w + sideGap, y: topPad, w: mapW, h: mapH}};
  const vario = {{x: map.x + map.w + sideGap, y: topPad, w: varioW, h: mapH}};
  const profile = {{x: map.x, y: map.y + map.h + profileGap, w: map.w + sideGap + vario.w, h: profileH}};
  return {{tape, map, vario, profile}};
}}

function mapScaleFor(map) {{
  const spanX = Math.max(1, bounds.maxX - bounds.minX);
  const spanY = Math.max(1, bounds.maxY - bounds.minY);
  return Math.min(map.w / spanX, map.h / spanY) * (mapView ? mapView.zoom : 1);
}}

function mapTransforms(map) {{
  if (!mapView) resetMapCamera();
  const scale = mapScaleFor(map);
  return {{
    scale,
    xToMap: x => map.x + map.w / 2 + (x - mapView.cx) * scale,
    yToMap: y => map.y + map.h / 2 - (y - mapView.cy) * scale,
    mapToX: px => mapView.cx + (px - (map.x + map.w / 2)) / scale,
    mapToY: py => mapView.cy - (py - (map.y + map.h / 2)) / scale
  }};
}}

function project3d(point, map, groundAlt = bounds.minAlt) {{
  if (!mapView) resetMapCamera();
  const scale = mapScaleFor(map) * 0.86;
  const verticalScale = 1.8;
  const x = mapView.cx - point.x;
  const y = point.y - mapView.cy;
  const z = ((point.z ?? point.detectorAlt ?? point.alt ?? groundAlt) - groundAlt - (mapView.cz || 0)) * verticalScale;
  const cy = Math.cos(mapView.yaw);
  const sy = Math.sin(mapView.yaw);
  const cp = Math.cos(mapView.pitch);
  const sp = Math.sin(mapView.pitch);
  const rx = x * cy - y * sy;
  const ry = x * sy + y * cy;
  const py = ry * cp - z * sp;
  const depth = ry * sp + z * cp;
  return {{
    x: map.x + map.w / 2 + mapView.panX + rx * scale,
    y: map.y + map.h / 2 + mapView.panY + py * scale,
    depth
  }};
}}

function drawPolyline3d(points, map, color, width, alpha = 1, ground = false) {{
  if (points.length < 2) return;
  ctx.save();
  ctx.globalAlpha = alpha;
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.beginPath();
  const first = project3d(ground ? {{x: points[0].x, y: points[0].y, z: bounds.minAlt}} : points[0], map);
  ctx.moveTo(first.x, first.y);
  for (const point of points.slice(1)) {{
    const next = project3d(ground ? {{x: point.x, y: point.y, z: bounds.minAlt}} : point, map);
    ctx.lineTo(next.x, next.y);
  }}
  ctx.stroke();
  ctx.restore();
}}

function drawVerticalSlice(points, map, currentT) {{
  const fadeSeconds = 60;
  const slicePoints = points.filter(point => currentT - point.t <= fadeSeconds);
  if (!slicePoints.length) return;
  ctx.save();
  ctx.strokeStyle = "#d8d8d8";
  ctx.lineWidth = 1;
  for (const point of slicePoints) {{
    const age = Math.max(0, currentT - point.t);
    const alpha = Math.max(0, 0.5 * (1 - age / fadeSeconds));
    if (alpha <= 0) continue;
    const top = project3d(point, map);
    const ground = project3d({{x: point.x, y: point.y, z: bounds.minAlt}}, map);
    ctx.globalAlpha = alpha;
    ctx.beginPath();
    ctx.moveTo(top.x, top.y);
    ctx.lineTo(ground.x, ground.y);
    ctx.stroke();
  }}
  ctx.restore();
}}

function drawGroundPlane(map) {{
  const xs = [bounds.minX, bounds.maxX];
  const ys = [bounds.minY, bounds.maxY];
  const corners = [
    {{x: xs[0], y: ys[0], z: bounds.minAlt}},
    {{x: xs[1], y: ys[0], z: bounds.minAlt}},
    {{x: xs[1], y: ys[1], z: bounds.minAlt}},
    {{x: xs[0], y: ys[1], z: bounds.minAlt}}
  ].map(p => project3d(p, map));
  ctx.save();
  ctx.fillStyle = "rgba(90, 120, 90, 0.16)";
  ctx.strokeStyle = "rgba(150, 190, 150, 0.34)";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(corners[0].x, corners[0].y);
  for (const corner of corners.slice(1)) ctx.lineTo(corner.x, corner.y);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  const span = Math.max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY);
  const step = niceGridStep(span / 4);
  for (let gx = Math.ceil(bounds.minX / step) * step; gx <= bounds.maxX; gx += step) {{
    const a = project3d({{x: gx, y: bounds.minY, z: bounds.minAlt}}, map);
    const b = project3d({{x: gx, y: bounds.maxY, z: bounds.minAlt}}, map);
    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    ctx.lineTo(b.x, b.y);
    ctx.stroke();
  }}
  for (let gy = Math.ceil(bounds.minY / step) * step; gy <= bounds.maxY; gy += step) {{
    const a = project3d({{x: bounds.minX, y: gy, z: bounds.minAlt}}, map);
    const b = project3d({{x: bounds.maxX, y: gy, z: bounds.minAlt}}, map);
    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    ctx.lineTo(b.x, b.y);
    ctx.stroke();
  }}
  ctx.restore();
}}

function followControlBox(map) {{
  return {{
    x: map.x + map.w - 88,
    y: map.y + 8,
    w: 78,
    h: 24
  }};
}}

function drawMapFollowControl(map) {{
  const box = followControlBox(map);
  followControlRect = box;
  ctx.save();
  ctx.fillStyle = "rgba(5, 5, 5, 0.62)";
  ctx.fillRect(box.x, box.y, box.w, box.h);
  ctx.strokeStyle = followCurrentFix ? "#84c8ff" : "#777";
  ctx.lineWidth = 1;
  ctx.strokeRect(box.x + 7, box.y + 6, 12, 12);
  if (followCurrentFix) {{
    ctx.strokeStyle = "#84c8ff";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(box.x + 9, box.y + 12);
    ctx.lineTo(box.x + 12, box.y + 16);
    ctx.lineTo(box.x + 18, box.y + 8);
    ctx.stroke();
  }}
  ctx.fillStyle = "#ddd";
  ctx.font = "12px system-ui, sans-serif";
  ctx.textAlign = "left";
  ctx.textBaseline = "middle";
  ctx.fillText("Follow", box.x + 26, box.y + box.h / 2);
  ctx.restore();
}}

function fillCenteredText(text, x, y, color, font = "11px system-ui, sans-serif") {{
  ctx.save();
  ctx.fillStyle = color;
  ctx.font = font;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(text, x, y);
  ctx.restore();
}}

function drawAltitudeMarker(x, y, w, value, fill, active, minY, maxY) {{
  const h = active ? 22 : 16;
  const top = Math.max(minY, Math.min(maxY - h, y - h / 2));
  ctx.fillStyle = fill;
  ctx.fillRect(x, top, w, h);
  fillCenteredText(`${{Math.round(value)}}m`, x + w / 2, top + h / 2, "#050505", active ? "14px system-ui, sans-serif" : "11px system-ui, sans-serif");
}}

function niceGridStep(span) {{
  const raw = Math.max(100, span / 6);
  const pow = 10 ** Math.floor(Math.log10(raw));
  for (const multiple of [1, 2, 5, 10]) {{
    const step = multiple * pow;
    if (raw <= step) return step;
  }}
  return 10 * pow;
}}

function pointInRect(point, rect) {{
  return point.x >= rect.x && point.x <= rect.x + rect.w && point.y >= rect.y && point.y <= rect.y + rect.h;
}}

function canvasPoint(event) {{
  const rect = canvas.getBoundingClientRect();
  return {{
    x: (event.clientX - rect.left) * canvas.width / rect.width,
    y: (event.clientY - rect.top) * canvas.height / rect.height
  }};
}}

function currentIndex() {{
  let lo = 0;
  let hi = processed.length - 1;
  while (lo < hi) {{
    const mid = Math.ceil((lo + hi) / 2);
    if (processed[mid].t <= replayT) lo = mid;
    else hi = mid - 1;
  }}
  return lo;
}}

function fmtTime(t) {{
  const h = Math.floor(t / 3600);
  const m = Math.floor((t % 3600) / 60);
  const s = Math.floor(t % 60);
  return `${{h}}:${{String(m).padStart(2, "0")}}:${{String(s).padStart(2, "0")}}`;
}}

function fmtDuration(t) {{
  const m = Math.floor(t / 60);
  const s = Math.floor(t % 60);
  return `${{m}}:${{String(s).padStart(2, "0")}}`;
}}

function drawPath(points, xScale, yScale, color, width) {{
  if (!points.length) return;
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.beginPath();
  ctx.moveTo(xScale(points[0].x), yScale(points[0].y));
  for (const p of points.slice(1)) ctx.lineTo(xScale(p.x), yScale(p.y));
  ctx.stroke();
}}

function resizeCanvasToDisplay() {{
  const rect = canvas.getBoundingClientRect();
  const nextW = Math.max(640, Math.round(rect.width));
  const nextH = Math.max(520, Math.round(rect.height));
  if (canvas.width !== nextW || canvas.height !== nextH) {{
    canvas.width = nextW;
    canvas.height = nextH;
  }}
}}

function updateScrubLayout(profile) {{
  const rect = canvas.getBoundingClientRect();
  const scaleX = rect.width / Math.max(1, canvas.width);
  replayStack.style.setProperty("--profile-left", `${{Math.round(profile.x * scaleX)}}px`);
  replayStack.style.setProperty("--profile-width", `${{Math.round(profile.w * scaleX)}}px`);
}}

function draw() {{
  if (!processed.length) return;
  resizeCanvasToDisplay();
  const i = currentIndex();
  const fix = processed[i];
  const elapsed = processed.slice(0, i + 1);
  const candidatePoints = elapsed.filter(p => p.candidate);
  const detected = detectThermalsThrough(processed, i);
  const saved = detected.saved;
  const failed = detected.failed;
  const merged = buildMergedThermals(saved);
  currentFrameItems = {{saved, failed, merged}};
  const W = canvas.width;
  const H = canvas.height;
  const {{tape, map, vario, profile}} = mapLayout();
  if (followCurrentFix) {{
    centerMapOnPosition(smoothedFollowPosition(i));
  }}
  updateScrubLayout(profile);
  const altRange = visibleAltitudeRange(processed);
  const altToY = alt => tape.y + tape.h - (alt - altRange.min) / Math.max(1, altRange.max - altRange.min) * tape.h;
  const profX = t => profile.x + t / Math.max(1, bounds.duration) * profile.w;
  const profY = alt => profile.y + profile.h - (alt - altRange.min) / Math.max(1, altRange.max - altRange.min) * profile.h;

  ctx.clearRect(0, 0, W, H);
  ctx.fillStyle = "#050505";
  ctx.fillRect(0, 0, W, H);
  ctx.font = "13px system-ui, sans-serif";
  ctx.textBaseline = "top";

  ctx.strokeStyle = "#444";
  ctx.lineWidth = 1;
  ctx.strokeRect(map.x, map.y, map.w, map.h);
  ctx.strokeRect(tape.x, tape.y, tape.w, tape.h);
  ctx.strokeRect(vario.x, vario.y, vario.w, vario.h);
  ctx.strokeRect(profile.x, profile.y, profile.w, profile.h);

  ctx.save();
  ctx.beginPath();
  ctx.rect(map.x, map.y, map.w, map.h);
  ctx.clip();
  drawGroundPlane(map);
  drawPolyline3d(processed, map, "#7aa67a", 1, 0.35, true);
  drawPolyline3d(processed, map, "#565656", 1, 0.58);
  drawPolyline3d(elapsed, map, "#d8d8d8", 2, 1);
  drawVerticalSlice(elapsed, map, fix.t);
  for (const p of candidatePoints) {{
    const point = project3d(p, map);
    ctx.fillStyle = "#84c8ff";
    ctx.fillRect(point.x - 1, point.y - 1, 2, 2);
  }}
  for (const th of saved) {{
    const point = project3d(th, map);
    ctx.strokeStyle = "#ffd166";
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.arc(point.x, point.y, 9, 0, Math.PI * 2);
    ctx.stroke();
    ctx.fillStyle = "#ffd166";
    ctx.fillText(`T${{th.id}}`, point.x + 11, point.y - 11);
  }}
  for (const candidate of failed) {{
    const point = project3d(candidate, map);
    ctx.strokeStyle = "#84c8ff";
    ctx.lineWidth = isSelected("candidate", candidate.id) ? 4 : 3;
    ctx.beginPath();
    ctx.arc(point.x, point.y, 9, 0, Math.PI * 2);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(point.x - 6, point.y);
    ctx.lineTo(point.x + 6, point.y);
    ctx.moveTo(point.x, point.y - 6);
    ctx.lineTo(point.x, point.y + 6);
    ctx.stroke();
    ctx.fillStyle = "#84c8ff";
    ctx.fillText(`C${{candidate.id}}`, point.x + 10, point.y + 8);
  }}
  for (const merge of merged) {{
    const point = project3d(merge, map);
    ctx.strokeStyle = "#9cff57";
    ctx.lineWidth = 1;
    for (const source of merge.sources) {{
      const sourcePoint = project3d(source, map);
      ctx.beginPath();
      ctx.moveTo(point.x, point.y);
      ctx.lineTo(sourcePoint.x, sourcePoint.y);
      ctx.stroke();
    }}
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.arc(point.x, point.y, 13, 0, Math.PI * 2);
    ctx.stroke();
    ctx.fillStyle = "#9cff57";
    ctx.fillText(compactMergedLabel(merge), point.x + 15, point.y + 2);
  }}
  const fixPoint = project3d(fix, map);
  ctx.fillStyle = "#fff";
  ctx.beginPath();
  ctx.arc(fixPoint.x, fixPoint.y, 6, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = "#bbb";
  ctx.fillText("3D: left/right pan, middle rotate, wheel zoom", map.x + 10, map.y + 10);
  drawMapFollowControl(map);
  ctx.restore();

  ctx.fillStyle = "#999";
  ctx.fillText(`${{altRange.max}}m`, tape.x - 12, tape.y - 18);
  ctx.fillText(`${{altRange.min}}m`, tape.x - 12, tape.y + tape.h + 4);
  ctx.strokeStyle = "#777";
  for (let a = Math.ceil(altRange.min / 250) * 250; a <= altRange.max; a += 250) {{
    const y = altToY(a);
    ctx.beginPath();
    ctx.moveTo(tape.x + tape.w - 12, y);
    ctx.lineTo(tape.x + tape.w, y);
    ctx.stroke();
  }}
  const altY = altToY(fix.detectorAlt);
  drawAltitudeMarker(tape.x, altY, tape.w, fix.detectorAlt, altitudeSource === "pressure" ? "#c9a0ff" : "#fff", true, tape.y, tape.y + tape.h);

  const mid = vario.y + vario.h / 2;
  ctx.strokeStyle = "#777";
  ctx.beginPath();
  ctx.moveTo(vario.x, mid);
  ctx.lineTo(vario.x + vario.w, mid);
  ctx.stroke();
  const clampedVario = Math.max(-7, Math.min(7, fix.vario));
  const barH = Math.abs(clampedVario) / 7 * (vario.h / 2);
  ctx.fillStyle = clampedVario >= 0 ? "#84c8ff" : "#ff8c78";
  if (clampedVario >= 0) ctx.fillRect(vario.x + 12, mid - barH, vario.w - 24, barH);
  else ctx.fillRect(vario.x + 12, mid, vario.w - 24, barH);
  ctx.fillStyle = "rgba(5, 5, 5, 0.78)";
  ctx.fillRect(vario.x + 5, mid - 10, vario.w - 10, 20);
  fillCenteredText(`${{fix.vario >= 0 ? "+" : ""}}${{fix.vario.toFixed(1)}}`, vario.x + vario.w / 2, mid, "#fff", "11px system-ui, sans-serif");
  ctx.fillStyle = "#999";
  ctx.fillText("+7", vario.x + 14, vario.y - 18);
  ctx.fillText("-7", vario.x + 14, vario.y + vario.h + 4);

  ctx.strokeStyle = "#555";
  ctx.beginPath();
  ctx.moveTo(profile.x, profY(altRange.min));
  ctx.lineTo(profile.x + profile.w, profY(altRange.min));
  ctx.stroke();
  const windowEndX = profX(fix.t);
  const displayWindowS = Number(pendingParams.window_s ?? params.window_s);
  const windowStartX = profX(Math.max(0, fix.t - displayWindowS));
  ctx.fillStyle = "rgba(132,200,255,0.16)";
  ctx.fillRect(windowStartX, profile.y, Math.max(1, windowEndX - windowStartX), profile.h);
  ctx.strokeStyle = altitudeSource === "pressure" ? "#c9a0ff" : "#d8d8d8";
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(profX(processed[0].t), profY(processed[0].detectorAlt));
  for (const p of processed.slice(1)) ctx.lineTo(profX(p.t), profY(p.detectorAlt));
  ctx.stroke();
  ctx.fillStyle = "rgba(132,200,255,0.85)";
  for (const p of elapsed) {{
    if (p.candidate) ctx.fillRect(profX(p.t), profY(p.detectorAlt) - 1, 2, 2);
  }}
  ctx.fillStyle = "#ffd166";
  for (const th of saved) {{
    const markerX = profX(th.end_s);
    const markerW = isSelected("thermal", th.id) ? 6 : 2;
    ctx.fillRect(markerX - markerW / 2, profile.y, markerW, profile.h);
    const label = `T${{th.id}}`;
    const labelX = Math.min(profile.x + profile.w - 22, Math.max(profile.x + 3, markerX + 4));
    ctx.fillText(label, labelX, profile.y + 4);
  }}
  ctx.fillStyle = "#84c8ff";
  for (const candidate of failed) {{
    const markerX = profX(candidate.end_s);
    const markerW = isSelected("candidate", candidate.id) ? 6 : 2;
    ctx.fillRect(markerX - markerW / 2, profile.y, markerW, profile.h);
    const label = `C${{candidate.id}}`;
    const labelX = Math.min(profile.x + profile.w - 22, Math.max(profile.x + 3, markerX + 4));
    ctx.fillText(label, labelX, profile.y + 18);
  }}
  ctx.fillStyle = "#9cff57";
  for (const merge of merged) {{
    const markerX = profX(merge.end_s);
    const markerW = isSelected("merged", merge.key) ? 6 : 2;
    ctx.fillRect(markerX - markerW / 2, profile.y, markerW, profile.h);
    const label = compactMergedLabel(merge);
    const labelX = Math.min(profile.x + profile.w - 46, Math.max(profile.x + 3, markerX + 4));
    ctx.fillText(label, labelX, profile.y + 32);
  }}
  ctx.strokeStyle = "#fff";
  ctx.beginPath();
  ctx.moveTo(profX(fix.t), profile.y);
  ctx.lineTo(profX(fix.t), profile.y + profile.h);
  ctx.stroke();
  ctx.fillStyle = "#bbb";
  ctx.fillText("Altitude profile", profile.x, profile.y - 18);
  ctx.fillStyle = altitudeSource === "pressure" ? "#c9a0ff" : "#d8d8d8";
  ctx.fillText(altitudeSource === "pressure" ? "IGC pressure" : "GPS", profile.x + 112, profile.y - 18);

  document.getElementById("sourceName").textContent = sourceName;
  document.getElementById("time").textContent = fmtTime(fix.t);
  document.getElementById("alt").textContent = `${{altitudeSource === "pressure" ? "Press" : "GPS"}} ${{fix.detectorAlt}} m`;
  document.getElementById("vario").textContent = `${{fix.vario.toFixed(2)}} m/s`;
  document.getElementById("gain").textContent = `${{fix.windowGain.toFixed(0)}} m`;
  document.getElementById("turn").textContent = `${{fix.windowTurn.toFixed(0)}} deg`;
  document.getElementById("detector").textContent = `${{fix.candidate ? "candidate" : "watching"}} (${{altitudeSourceLabel()}})`;
  document.getElementById("detector").className = fix.candidate ? "candidate" : "";
  document.getElementById("saved").textContent = String(saved.length);
  document.getElementById("saved").className = saved.length ? "ok" : "";
  document.getElementById("thermalList").innerHTML = saved.map(th =>
    `<article class="thermal-card${{selectedClass("thermal", th.id)}}" data-select-kind="thermal" data-select-id="${{th.id}}">` +
      `<strong>T${{th.id}}</strong>` +
      `<div><span>Gain</span><b>${{th.gain_m.toFixed(0)}} m</b></div>` +
      `<div><span>Time</span><b>${{fmtDuration(th.duration_s)}}</b></div>` +
      `<div><span>Climb</span><b>+${{th.avg_climb_mps.toFixed(1)}} m/s</b></div>` +
      `<div><span>Turns</span><b>${{th.turns.toFixed(1)}}</b></div>` +
    `</article>`
  ).join("");
  document.getElementById("candidateList").innerHTML = failed.map(candidate =>
    `<article class="candidate-card${{selectedClass("candidate", candidate.id)}}" data-select-kind="candidate" data-select-id="${{candidate.id}}">` +
      `<strong>C${{candidate.id}}</strong>` +
      `<div class="${{candidate.failed_gain ? "fail" : "pass"}}"><span>Save gain</span><b>${{candidate.gain_m.toFixed(0)}} / ${{params.min_save_gain_m}} m</b></div>` +
      `<div class="${{candidate.failed_duration ? "fail" : "pass"}}"><span>Save duration</span><b>${{fmtDuration(candidate.duration_s)}} / ${{fmtDuration(params.min_duration_s)}}</b></div>` +
      `<div class="pass"><span>Climb</span><b>+${{candidate.avg_climb_mps.toFixed(1)}} m/s</b></div>` +
      `<div class="pass"><span>Turns</span><b>${{candidate.turns.toFixed(1)}}</b></div>` +
    `</article>`
  ).join("");
  document.getElementById("mergedList").innerHTML = merged.map(merge =>
    `<article class="merged-card${{selectedClass("merged", merge.key)}}" data-select-kind="merged" data-select-id="${{merge.key}}">` +
      `<strong>${{merge.label}} Merged</strong>` +
      `<div><span>Gain</span><b>${{merge.gain_m.toFixed(0)}} m</b></div>` +
      `<div><span>Time</span><b>${{fmtDuration(merge.duration_s)}}</b></div>` +
      `<div><span>Climb</span><b>+${{merge.avg_climb_mps.toFixed(1)}} m/s</b></div>` +
      `<div><span>Turns</span><b>${{merge.turns.toFixed(1)}}</b></div>` +
    `</article>`
  ).join("");
  scrub.value = String(Math.round(fix.t / Math.max(1, bounds.duration) * 1000));
}}

function buildParamControls() {{
  const box = document.getElementById("params");
  box.innerHTML = "";
  for (const [groupLabel, keys] of paramGroups) {{
    const group = document.createElement("section");
    group.className = "param-group";
    group.innerHTML = `<h3>${{groupLabel}}</h3>`;
    box.appendChild(group);
    for (const key of keys) {{
      const [, label, min, max, step, unit, desc] = paramByKey.get(key);
      const row = document.createElement("label");
      row.className = "param";
      row.innerHTML = `<span>${{label}}</span><input id="range-${{key}}" type="range" min="${{min}}" max="${{max}}" step="${{step}}"><input id="num-${{key}}" type="number" min="${{min}}" max="${{max}}" step="${{step}}"><span class="param-desc">${{desc}}</span>`;
      group.appendChild(row);
      const range = document.getElementById(`range-${{key}}`);
      const num = document.getElementById(`num-${{key}}`);
      range.value = pendingParams[key];
      num.value = pendingParams[key];
      const update = value => {{
        if (playing) return;
        pendingParams[key] = Number(value);
        range.value = pendingParams[key];
        num.value = pendingParams[key];
        markParamsDirty();
      }};
      range.addEventListener("input", () => update(range.value));
      num.addEventListener("change", () => update(num.value));
      row.title = unit;
    }}
  }}
  updateParamDisabled();
}}

function updateSummary() {{
  const pressureAvailable = hasPressureAltitude(rawFixes);
  helpRunSummary.textContent =
    `This run has ${{processed.length.toLocaleString()}} valid fixes. ` +
    `The realtime detector is using ${{altitudeSourceLabel()}}. ` +
    `${{pressureAvailable ? "Only the selected altitude source is shown in the map and profile." : "Only GPS altitude is available in this file."}}`;
}}

function markParamsDirty() {{
  paramsDirty = pendingAltitudeSource !== altitudeSource ||
    paramSpecs.some(([key]) => pendingParams[key] !== params[key]);
  playing = false;
  playPause.textContent = "Play";
  playPause.disabled = paramsDirty;
  document.getElementById("runState").textContent = paramsDirty
    ? "Detector settings changed. Restart the replay to apply them to a clean simulated detector state."
    : "Realtime replay: saved thermals appear only after the simulated detector saves them.";
  updateParamDisabled();
  draw();
}}

function updateParamDisabled() {{
  for (const [key] of paramSpecs) {{
    const range = document.getElementById(`range-${{key}}`);
    const num = document.getElementById(`num-${{key}}`);
    if (range) range.disabled = playing;
    if (num) num.disabled = playing;
  }}
  resetDefaults.disabled = playing;
  fileInput.disabled = playing;
  restartWithParams.disabled = !paramsDirty || playing;
  ensureAltitudeControls();
}}

function restartRunWithPendingParams() {{
  params = {{...pendingParams}};
  altitudeSource = pendingAltitudeSource;
  paramsDirty = false;
  playing = false;
  replayT = 0;
  playPause.textContent = "Play";
  playPause.disabled = false;
  recomputeMetrics();
  document.getElementById("runState").textContent =
    "Realtime replay: saved thermals appear only after the simulated detector saves them.";
  updateParamDisabled();
}}

function tick(now) {{
  const dt = (now - lastNow) / 1000;
  lastNow = now;
  if (playing) {{
    replayT += dt * Number(speed.value);
    if (replayT >= bounds.duration) {{
      replayT = bounds.duration;
      playing = false;
      playPause.textContent = "Play";
      updateParamDisabled();
    }}
  }}
  draw();
  requestAnimationFrame(tick);
}}

playPause.addEventListener("click", () => {{
  if (paramsDirty) return;
  playing = !playing;
  playPause.textContent = playing ? "Pause" : "Play";
  lastNow = performance.now();
  updateParamDisabled();
}});
speed.addEventListener("input", () => speedLabel.textContent = `${{speed.value}}x`);
scrub.addEventListener("input", () => {{
  replayT = Number(scrub.value) / 1000 * bounds.duration;
}});
restartWithParams.addEventListener("click", restartRunWithPendingParams);
resetMapView.addEventListener("click", () => {{
  resetMapCamera();
  draw();
}});
howThisWorks.addEventListener("click", () => {{
  updateSummary();
  if (howDialog.showModal) howDialog.showModal();
  else howDialog.setAttribute("open", "");
}});
closeHowDialog.addEventListener("click", () => {{
  if (howDialog.close) howDialog.close();
  else howDialog.removeAttribute("open");
}});
howDialog.addEventListener("click", event => {{
  if (event.target === howDialog) howDialog.close();
}});
function selectAltitudeSource(source) {{
  if (playing) return;
  if (source === "pressure" && !hasPressureAltitude(rawFixes)) source = "gps";
  pendingAltitudeSource = source;
  ensureAltitudeControls();
  markParamsDirty();
}}
showGpsAlt.addEventListener("change", () => selectAltitudeSource("gps"));
showPressureAlt.addEventListener("change", () => selectAltitudeSource("pressure"));
document.getElementById("thermalList").addEventListener("pointerdown", handleCardListClick);
document.getElementById("candidateList").addEventListener("pointerdown", handleCardListClick);
document.getElementById("mergedList").addEventListener("pointerdown", handleCardListClick);
resetDefaults.addEventListener("click", () => {{
  if (playing) return;
  pendingParams = {{...defaultParams}};
  pendingAltitudeSource = preferredAltitudeSource(rawFixes);
  buildParamControls();
  markParamsDirty();
}});
canvas.addEventListener("wheel", event => {{
  if (!bounds) return;
  resizeCanvasToDisplay();
  const {{map}} = mapLayout();
  const point = canvasPoint(event);
  if (!pointInRect(point, map)) return;
  event.preventDefault();
  if (!mapView) resetMapCamera();
  const zoomFactor = Math.exp(event.deltaY * 0.001);
  mapView.zoom = Math.max(0.35, Math.min(80, mapView.zoom * zoomFactor));
  draw();
}}, {{passive: false}});
canvas.addEventListener("contextmenu", event => {{
  const point = canvasPoint(event);
  const {{map}} = mapLayout();
  if (pointInRect(point, map)) event.preventDefault();
}});
canvas.addEventListener("pointerdown", event => {{
  if (!bounds) return;
  resizeCanvasToDisplay();
  const {{map}} = mapLayout();
  const point = canvasPoint(event);
  if (!pointInRect(point, map)) return;
  followControlRect = followControlBox(map);
  if (event.button === 0 && followControlRect && pointInRect(point, followControlRect)) {{
    followCurrentFix = !followCurrentFix;
    if (followCurrentFix) {{
      centerMapOnPosition(smoothedFollowPosition(currentIndex()));
    }}
    event.preventDefault();
    draw();
    return;
  }}
  isPanning = true;
  mapDragMode = event.button === 1 ? "rotate" : "pan";
  lastPan = point;
  canvas.classList.add("is-panning");
  event.preventDefault();
  if (canvas.setPointerCapture) canvas.setPointerCapture(event.pointerId);
}});
canvas.addEventListener("pointermove", event => {{
  if (!isPanning || !lastPan || !bounds) return;
  const point = canvasPoint(event);
  const dx = point.x - lastPan.x;
  const dy = point.y - lastPan.y;
  if (mapDragMode === "rotate") {{
    mapView.yaw += dx * 0.008;
    mapView.pitch = Math.max(0.18, Math.min(1.42, mapView.pitch - dy * 0.006));
  }} else {{
    mapView.panX += dx;
    mapView.panY += dy;
  }}
  lastPan = point;
  draw();
}});
function endPan(event) {{
  if (!isPanning) return;
  isPanning = false;
  mapDragMode = null;
  lastPan = null;
  canvas.classList.remove("is-panning");
  if (canvas.releasePointerCapture) canvas.releasePointerCapture(event.pointerId);
}}
canvas.addEventListener("pointerup", endPan);
canvas.addEventListener("pointercancel", endPan);
fileInput.addEventListener("change", async () => {{
  const file = fileInput.files && fileInput.files[0];
  if (!file) return;
  const text = await file.text();
  try {{
    rawFixes = parseIgc(text);
    sourceName = file.name;
    params = {{...pendingParams}};
    altitudeSource = preferredAltitudeSource(rawFixes);
    pendingAltitudeSource = altitudeSource;
    paramsDirty = false;
    replayT = 0;
    playing = false;
    playPause.textContent = "Play";
    playPause.disabled = false;
    recomputeMetrics();
    document.getElementById("runState").textContent =
      "Realtime replay: saved thermals appear only after the simulated detector saves them.";
    updateParamDisabled();
  }} catch (err) {{
    alert(err.message || String(err));
  }}
}});

buildParamControls();
recomputeMetrics();
speedLabel.textContent = `${{speed.value}}x`;
updateParamDisabled();
if ("ResizeObserver" in window) {{
  new ResizeObserver(() => draw()).observe(canvas);
}} else {{
  window.addEventListener("resize", draw);
}}
requestAnimationFrame(tick);
</script>
</body>
</html>
"""


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a Leaf GPS thermal detector replay from an IGC file.")
    parser.add_argument("igc", type=Path)
    parser.add_argument("--out", type=Path, default=Path(__file__).with_name("igc_thermal_replay.html"))
    args = parser.parse_args()
    payload = build_payload(args.igc)
    args.out.write_text(render_html(payload), encoding="utf-8")
    print(f"Wrote {args.out}")
    print(json.dumps({"source": payload["source"], "valid_fix_count": len(payload["fixes"])}, indent=2))
    print(json.dumps(payload["defaults"], indent=2))


if __name__ == "__main__":
    main()
