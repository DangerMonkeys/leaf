#!/usr/bin/env python3
"""Render frozen Leaf runtime telemetry; Python standard library only."""

import argparse
import hashlib
import html
import json
import math
from pathlib import Path

SCHEMA = 3
WORK_KEYS = ("tasks_us", "gps_us", "services_us", "poll_us")
COMPATIBILITY = ("schema_version", "source", "hardware", "work_clock", "cpu_mhz", "capture_bytes", "capture_memory", "settings")
PROFILE_KEYS = ("profile", "scenario", "toolchain")


def report_fields(value):
    """Omit obsolete memory fields from legacy captures; raw evidence stays untouched."""
    if isinstance(value, dict):
        return {k: report_fields(v) for k, v in value.items() if not k.startswith("psram_")}
    if isinstance(value, list):
        return [report_fields(v) for v in value]
    return value


def validate(capture):
    if not isinstance(capture, dict):
        raise ValueError("Capture must be a JSON object")
    if capture.get("schema_version") not in (1, 2, SCHEMA):
        raise ValueError("Unsupported capture schema")
    if capture.get("source") not in ("device", "simulator"):
        raise ValueError("Unknown measurement source")
    expected_clock = "elapsed" if capture["source"] == "device" else "host_thread_cpu"
    if capture.get("work_clock") != expected_clock:
        raise ValueError("Source and work clock disagree")
    if capture.get("end_us", 0) <= capture.get("start_us", -1):
        raise ValueError("Capture has no positive frozen duration")
    slots = capture.get("slots", [])
    if len(slots) != 100 or [s.get("slot") for s in slots] != list(range(100)):
        raise ValueError("Capture must contain each of the 100 slots in order")
    bounds = capture.get("histogram_upper_us", [])
    if not bounds or bounds != sorted(set(bounds)):
        raise ValueError("Invalid histogram bounds")
    for slot in slots:
        for key in (*WORK_KEYS, "count", "deadline_misses", "duration_overruns", "skipped_ticks",
                    "max_occupied_us", "max_start_delay_us", "max_end_delay_us"):
            if not isinstance(slot.get(key), int) or slot[key] < 0:
                raise ValueError(f"Invalid {key} in slot {slot['slot']}")
        histogram = slot.get("histogram", [])
        if len(histogram) != len(bounds) or any(not isinstance(n, int) or n < 0 for n in histogram):
            raise ValueError("Invalid histogram counts")
        if sum(histogram) != slot["count"]:
            raise ValueError("Histogram counts disagree with completed slot count")
        if slot["deadline_misses"] > slot["count"] or slot["duration_overruns"] > slot["count"]:
            raise ValueError("Miss count exceeds sample count")
    if capture["source"] == "simulator" and capture.get("memory_supported"):
        raise ValueError("Simulator device heap placeholders cannot be reported as measurements")
    if capture["schema_version"] >= 2:
        pages = capture.get("pages", [])
        if [p.get("context") for p in pages] != list(range(10, 17)):
            raise ValueError("Capture must contain all seven non-menu page contexts")
        for p in pages:
            for key in ("count", "total_us", "max_render_us", "slots", "max_slot_occupied_us",
                        "deadline_misses", "button_excluded", "recovery_excluded"):
                if not isinstance(p.get(key), int) or p[key] < 0:
                    raise ValueError("Invalid page timing")
            if (len(p.get("histogram", [])) != len(bounds) or
                    any(not isinstance(n, int) or n < 0 for n in p["histogram"]) or
                    sum(p["histogram"]) != p["count"] or p["deadline_misses"] > p["slots"]):
                raise ValueError("Invalid page counts")
        for slot in slots:
            for key in ("button_excluded", "recovery_excluded", "recovery_deadline_misses",
                        "recovery_skipped_ticks", "max_recovery_start_delay_us",
                        "max_recovery_end_delay_us", "max_recovery_baro_start_delay_us",
                        "max_recovery_occupied_us"):
                if not isinstance(slot.get(key), int) or slot[key] < 0:
                    raise ValueError("Invalid interaction accounting")
            if slot["recovery_deadline_misses"] > slot["recovery_excluded"]:
                raise ValueError("Invalid recovery misses")
        for key, count_field in (("button_slots", "button_excluded"), ("recovery_slots", "recovery_excluded")):
            if capture.get(key, {}).get("count") != sum(s[count_field] for s in slots):
                raise ValueError("Interaction totals disagree with per-slot counts")
    if capture["schema_version"] >= 3:
        validate_gps(capture.get("gps_profile"))


def validate_gps(g):
    if not isinstance(g, dict):
        raise ValueError("Missing GPS sentence profile")
    for key in ("total", "unassigned", "partial_uart", "excluded_async", "nested_dropped", "uart_resets",
                "recent_capacity", "worst_capacity", "recent_overwritten", "record_max_us"):
        if not isinstance(g.get(key), int) or g[key] < 0:
            raise ValueError("Invalid GPS profile counters")
    if len(g.get("stage_names", [])) != 7:
        raise ValueError("Invalid GPS stage names")
    bounds = g.get("histogram_upper_us", [])
    if not bounds or bounds != sorted(set(bounds)):
        raise ValueError("Invalid GPS histogram bounds")
    types = g.get("types", [])
    if [t.get("type") for t in types] != list(range(8)):
        raise ValueError("Missing GPS sentence types")
    for t in types:
        for key in ("count", "accepted", "rejected", "unobserved", "deadline_crossings", "total_us",
                    "max_processing_us", "max_end_delay_us"):
            if not isinstance(t.get(key), int) or t[key] < 0:
                raise ValueError("Invalid GPS type counters")
        for field, size in (("stage_total_us", 7), ("stage_max_us", 7), ("histogram", len(bounds))):
            if len(t.get(field, [])) != size or any(not isinstance(v, int) or v < 0 for v in t[field]):
                raise ValueError("Invalid GPS stages or histogram")
        if (sum(t["histogram"]) != t["count"] or
                t["accepted"] + t["rejected"] + t["unobserved"] != t["count"] or
                t["deadline_crossings"] > t["count"] or sum(t["stage_total_us"]) > t["total_us"] or
                any(v > t["max_processing_us"] for v in t["stage_max_us"])):
            raise ValueError("Inconsistent GPS type counts or overlapping stages")
    slots = g.get("start_slots", [])
    if (len(slots) != 100 or any(not isinstance(v, int) or v < 0 for v in slots) or
            sum(t["count"] for t in types) != g["total"] or sum(slots) + g["unassigned"] != g["total"] or
            g["partial_uart"] > g["total"] or
            len(g.get("recent", [])) != min(g["total"], g["recent_capacity"]) or
            len(g.get("worst", [])) > g["worst_capacity"]):
        raise ValueError("Inconsistent GPS coverage or trace counts")
    for sample in g["recent"] + g["worst"]:
        for key in ("type", "validity", "slot", "tick", "flags", "length", "first_read_us", "ready_us",
                    "end_us", "expected_us", "processing_us"):
            if not isinstance(sample.get(key), int) or sample[key] < 0:
                raise ValueError("Invalid GPS sentence trace")
        if (sample["type"] >= 8 or sample["validity"] > 2 or sample["flags"] > 31 or
                (not sample["flags"] & 16 and sample["slot"] >= 100) or
                not sample["first_read_us"] <= sample["ready_us"] <= sample["end_us"] or
                len(sample.get("stage_us", [])) != 7 or
                any(not isinstance(v, int) or v < 0 for v in sample["stage_us"]) or
                sum(sample["stage_us"]) > sample["processing_us"]):
            raise ValueError("Invalid GPS timestamps or overlapping stages")
    for sample in g["worst"]:
        if sample["flags"] & 16 or sample["end_us"] <= sample["expected_us"] + 10000:
            raise ValueError("GPS worst trace is not a deadline crossing")


def gps_incomplete(capture):
    g = capture.get("gps_profile", {})
    return bool(g.get("excluded_async") or g.get("nested_dropped") or
                any(t["unobserved"] for t in g.get("types", [])))


def percentile_upper(slot, bounds, fraction):
    """Histogram upper bound, not an exact percentile or a worst-case guarantee."""
    if not slot["count"]:
        return None
    target = math.ceil(slot["count"] * fraction)
    total = 0
    for upper, count in zip(bounds, slot["histogram"]):
        total += count
        if total >= target:
            return min(upper, slot["max_occupied_us"])
    raise ValueError("Histogram is incomplete")


def assess(capture, metadata, baseline=None):
    validate(capture)
    notes = []
    modern = capture["schema_version"] >= 2
    if not modern:
        notes.append("Legacy capture: button/recovery samples were not separated and per-page render timings are unavailable. Recapture for the revised baseline.")
    else:
        notes.append("Normal slot/page statistics exclude held buttons, button events and recovery. Recovery covers the next two dispatched slots and extends while completion is late or start delay exceeds 1 ms; inspect it separately.")
    empty = [s["slot"] for s in capture["slots"] if not s["count"]]
    if empty:
        notes.append(f"No complete samples in {len(empty)} slots; coverage is incomplete.")
    minimum = min(s["count"] for s in capture["slots"])
    if minimum < 100:
        notes.append(f"Few samples: the least-sampled slot has {minimum}; tail estimates are preliminary.")
    for key in ("commit", "firmware_sha256", *PROFILE_KEYS):
        if not metadata.get(key):
            notes.append(f"Missing provenance: {key}.")
    if capture["source"] == "simulator":
        notes.append("Host CPU work is uncalibrated. Bars are not ESP32 utilization; deadline crossings use virtual time.")
        notes.append("Device heap and stack measurements are unavailable in the simulator.")
    if capture.get("unmeasured_async_segments", 0):
        notes.append("Simulator background/UDP callbacks were excluded from loop-thread totals; workload coverage is incomplete.")
    gps = capture.get("gps_profile")
    if gps is None:
        notes.append("GPS sentence profiling is unavailable in this older capture; sentence type, validity and stage timing cannot be reconstructed.")
    else:
        notes.append("GPS summaries include all sentence dispatches, including interactions. Stages are exclusive elapsed times on device (host thread CPU in sim), not isolated CPU costs. UART timestamps observe firmware reads, not wire arrival. Checksum acceptance does not establish a valid position fix.")
        if not gps["total"]:
            notes.append("No complete GPS sentence dispatches observed; sentence handling is untested.")
        if gps["partial_uart"]:
            notes.append("The first UART sentence(s) contained bytes read before capture; their ingestion timings are partial.")
        if gps_incomplete(capture):
            notes.append("GPS profiling coverage is incomplete: asynchronous/nested dispatches or missing parser results are disclosed in the GPS section.")
        if gps["uart_resets"]:
            notes.append(f"ATTENTION: {gps['uart_resets']} UART sentence-buffer resets occurred.")
    if capture.get("settings_changed"):
        notes.append("Settings changed during capture; split configurations before comparing baselines.")
    if capture.get("trace_overwritten", 0):
        notes.append("Recent trace wrapped by design; full-run slot aggregates and the worst incident remain available.")
    if capture.get("memory_overwritten", 0):
        notes.append("Memory history wrapped; first sample and full-run sampled minima are retained.")
    if any(capture.get("settings", {}).get(k) for k in ("network_log", "web_log", "vario_log", "fanet_log")):
        notes.append("Other diagnostic SD logging was enabled and can affect timing.")
    notes.append("Clock-pair, slot-bookkeeping, and memory-sampling costs are reported. Full instrumentation overhead requires an external A/B measurement.")
    if capture.get("memory_supported"):
        notes.append("A zero stack reading can mean an unregistered task or no headroom; inspect active task state and raw samples. Positive stack minima skip zero readings.")
    notes.append("Stage 1 manual capture: enabled features are not verified workload coverage. Review is required before release.")

    misses = sum(s["deadline_misses"] for s in capture["slots"])
    overruns = sum(s["duration_overruns"] for s in capture["slots"])
    skipped = sum(s["skipped_ticks"] for s in capture["slots"])
    changes = []
    reasons = []
    if baseline:
        old = baseline["capture"]
        old_meta = baseline.get("metadata", {})
        validate(old)
        for key in COMPATIBILITY:
            if capture.get(key) != old.get(key):
                reasons.append(f"Different {key}")
        for key in PROFILE_KEYS:
            if not metadata.get(key) or metadata.get(key) != old_meta.get(key):
                reasons.append(f"Missing or different {key}")
        if capture.get("settings_changed") or old.get("settings_changed"):
            reasons.append("Settings changed within a capture")
        old_duration = old["end_us"] - old["start_us"]
        duration = capture["end_us"] - capture["start_us"]
        if abs(duration - old_duration) > old_duration * .1:
            reasons.append("Measurement durations differ by more than 10%")
        if any(not s["count"] for s in old["slots"]) or empty:
            reasons.append("Incomplete slot coverage")
        if capture.get("unmeasured_async_segments", 0) or old.get("unmeasured_async_segments", 0):
            reasons.append("Unmeasured asynchronous simulator work")
        if gps_incomplete(capture) or gps_incomplete(old):
            reasons.append("Incomplete GPS sentence profiling")
        if not reasons:
            for new, previous in zip(capture["slots"], old["slots"]):
                if not new["count"] or not previous["count"]:
                    continue
                delta = new["max_occupied_us"] - previous["max_occupied_us"]
                changes.append({"slot": new["slot"], "label": new["label"],
                                "max_delta_us": delta,
                                "deadline_miss_rate_delta": new["deadline_misses"] / new["count"] -
                                previous["deadline_misses"] / previous["count"]})
            changes.sort(key=lambda s: s["max_delta_us"], reverse=True)
        else:
            notes.append("Baseline comparison withheld: " + "; ".join(reasons) + ".")

    memory = [capture["first_memory"]] if capture.get("first_memory") else []
    memory += capture.get("memory", [])
    memory_summary = None
    if capture.get("memory_supported") and memory:
        first, last = memory[0], memory[-1]
        memory_summary = {
            "internal_free_change": last["internal_free"] - first["internal_free"],
            "sampled_minima": {key: value for key, value in capture["memory_minima"].items()
                               if key != "time_us" and not key.startswith("psram_")} if capture.get("memory_minima") else {
                key: min(s[key] for s in memory) for key in first if key != "time_us" and not key.startswith("psram_")},
            "failed_allocations": capture.get("failed_allocations"),
        }
        notes.append("Memory change is between manual capture endpoints; a negative change alone does not prove a leak. Heap low watermarks include earlier boot activity.")
        if capture.get("failed_allocations"):
            notes.append(f"ATTENTION: {capture['failed_allocations']} allocation failures occurred during capture.")
        if memory_summary["internal_free_change"] < 0:
            notes.append("ATTENTION: internal free heap ended below its starting value; repeat equivalent lifecycle phases to assess recovery.")
    baro_late = [s["slot"] for s in capture["slots"] if s.get("max_baro_start_delay_us", 0) > 1000]
    if capture["source"] == "device" and baro_late:
        notes.append("ATTENTION: barometer first-call delay exceeded 1 ms in slots " +
                     ", ".join(f"{i:02d}" for i in baro_late) + ". Check conversion timing.")
    recovery = capture.get("recovery_slots", {})
    if modern and recovery.get("count", 0):
        if capture["source"] == "device" and (recovery.get("deadline_misses") or
                recovery.get("skipped_ticks") or recovery.get("max_baro_start_delay_us", 0) > 1000):
            notes.append("ATTENTION: button recovery includes deadline crossings, coalesced ticks or barometer delays. Button exclusions do not establish safe recovery; check sensor/GPS/IGC continuity on hardware.")
    pages = []
    for p in capture.get("pages", []):
        pages.append({**p, "average_render_us": p["total_us"] / p["count"] if p["count"] else None,
                      "p95_render_upper_us": percentile_upper(
                          {**p, "max_occupied_us": p["max_render_us"]}, capture["histogram_upper_us"], .95)})

    gps_summary = None
    if gps is not None:
        gps_types = []
        for t in gps["types"]:
            gps_types.append({**t, "average_processing_us": t["total_us"] / t["count"] if t["count"] else None,
                              "p95_processing_upper_us": percentile_upper(
                                  {**t, "max_occupied_us": t["max_processing_us"]}, gps["histogram_upper_us"], .95),
                              "other_us": t["total_us"] - sum(t["stage_total_us"])})
        gps_summary = {"total": gps["total"], "types": gps_types,
                       "sentence_deadline_crossings": sum(t["deadline_crossings"] for t in gps["types"]),
                       "coverage_incomplete": gps_incomplete(capture)}

    return {"status": "incomplete" if empty or capture.get("unmeasured_async_segments", 0) or gps_incomplete(capture) else "review_required",
            "duration_s": (capture["end_us"] - capture["start_us"]) / 1e6,
            "minimum_slot_samples": minimum, "deadline_misses": misses,
            "duration_overruns": overruns, "skipped_ticks": skipped,
            "baseline_comparable": bool(baseline) and not reasons,
            "baseline_reasons": reasons, "largest_changes": changes[:10],
            "memory": memory_summary, "pages": pages, "button_slots": capture.get("button_slots"),
            "recovery_slots": capture.get("recovery_slots"), "gps": gps_summary, "notes": notes}


def render_gps(c, assessment):
    esc = lambda value: html.escape(str(value), quote=True)
    g, summary = c.get("gps_profile"), assessment.get("gps")
    if g is None:
        return '<h2>GPS sentence handling</h2><p>Not available in this capture. A new capture with sentence profiling is required.</p>'
    rows, stages = [], []
    for t in summary["types"]:
        if not t["count"]:
            rows.append(f'<tr><td>{esc(t["label"])}</td><td colspan="7">Not measured</td></tr>')
            continue
        n = t["count"]
        rows.append(f'<tr><td>{esc(t["label"])}</td><td>{n}</td>'
                    f'<td>{t["accepted"]}/{t["rejected"]}/{t["unobserved"]}</td>'
                    f'<td>{t["average_processing_us"] / 1000:.3f}</td>'
                    f'<td>{t["max_processing_us"] / 1000:.3f}</td>'
                    f'<td>≤{t["p95_processing_upper_us"] / 1000:.3f}</td>'
                    f'<td>{t["deadline_crossings"]}</td><td>{t["max_end_delay_us"] / 1000:.3f}</td></tr>')
        values = ''.join(f'<td>{total / n / 1000:.3f} ({maximum / 1000:.3f})</td>'
                         for total, maximum in zip(t["stage_total_us"], t["stage_max_us"]))
        stages.append(f'<tr><td>{esc(t["label"])}</td>{values}<td>{t["other_us"] / n / 1000:.3f}</td></tr>')
    maximum = max(1, max(g["start_slots"]))
    bars = ''.join(f'<rect x="{i*10}" y="{80-count/maximum*75:.2f}" width="8" '
                   f'height="{count/maximum*75:.2f}" fill="#487885"><title>Slot {i:02d}: {count} sentence dispatches</title></rect>'
                   for i, count in enumerate(g["start_slots"]))
    labels = ''.join(f'<text x="{i*10}" y="96" font-size="11">{i:02d}</text>' for i in range(0, 100, 10))
    stage_headers = ''.join(f'<th>{esc(name)}</th>' for name in g["stage_names"])

    def events_table(events):
        event_rows = []
        for s in events:
            unassigned = s["flags"] & 16
            ready = '—' if unassigned else f'{(s["ready_us"]-s["expected_us"]) / 1000:.3f}'
            end = '—' if unassigned else f'{(s["end_us"]-s["expected_us"]) / 1000:.3f}'
            flags = ['injected' if s["flags"] & 1 else 'UART']
            for mask, text in ((2, 'partial ingestion'), (4, 'button seen'), (8, 'recovery'), (16, 'no slot')):
                if s["flags"] & mask: flags.append(text)
            detail = ', '.join(f'{name}: {v} µs' for name, v in zip(g["stage_names"], s["stage_us"]))
            validity = ('unobserved', 'rejected', 'accepted')[s["validity"]]
            event_rows.append(f'<tr title="{esc(detail)}"><td>{esc(s["talker"])}{esc(g["types"][s["type"]]["label"])}</td>'
                              f'<td>{validity}</td><td>{s["slot"] if not unassigned else "—"}/{s["tick"]}</td>'
                              f'<td>{ready}</td><td>{end}</td><td>{s["processing_us"] / 1000:.3f}</td>'
                              f'<td>{(s["ready_us"]-s["first_read_us"]) / 1000:.3f}</td>'
                              f'<td>{esc(", ".join(flags))}</td></tr>')
        return ('<p>No retained events.</p>' if not event_rows else
                '<table><tr><th>Sentence</th><th>Checksum/parser</th><th>Slot/tick</th><th>Ready +ms</th>'
                '<th>End +ms</th><th>Processing ms</th><th>Observed read span ms</th><th>Context</th></tr>' +
                ''.join(event_rows) + '</table>')

    worst = sorted(g["worst"], key=lambda s: s["end_us"]-s["expected_us"], reverse=True)
    return f'''<h2>GPS sentence handling</h2>
<p>{g['total']} completed dispatches · {summary['sentence_deadline_crossings']} sentence deadline crossings.
Sentence and slot crossing counts differ: multiple sentences may share a slot. These summaries include button/recovery activity.</p>
<table><tr><th>NMEA</th><th>Count</th><th>Accepted/rejected/unobserved</th><th>Mean ms</th><th>Max ms</th><th>p95 ms</th><th>Crossings</th><th>Max end +ms</th></tr>{''.join(rows)}</table>
<p>Checksum acceptance is separate from a valid position fix. Processing combines active UART-read chunks and complete-line dispatch; idle waiting between fragments is excluded. On device, elapsed times include blocking and preemption; simulator values use host thread CPU time.</p>
<details open><summary>Processing stages · mean (maximum) ms</summary><p>Stages are exclusive, including nested mutex waits. GPS updates means immediate sentence updates; scheduled navigation/glide work remains in slots 09/59. Other is remaining measured processing time. UART reads are absent for injected messages; separate captures should be used for distinct input sources.</p>
<table><tr><th>NMEA</th>{stage_headers}<th>Other mean</th></tr>{''.join(stages)}</table></details>
<h3>Sentence dispatch phase across the 100 logical slots</h3>
<svg viewBox="0 0 1000 100" role="img" aria-label="GPS sentence dispatch counts by logical slot">{bars}{labels}</svg>
<p>Phase is observed after a line is assembled, before dispatch. It is not the receiver's wire-arrival timestamp; queued bytes can be older. Logical phase can shift after coalesced ticks. Injected input reflects its playback schedule.</p>
<h3>Retained sentence deadline crossings</h3><p>Ready/end offsets are relative to the slot's intended start; end beyond +10 ms crosses its deadline. Observed read span may include inter-fragment waiting and is not CPU cost. Hover for stage durations.</p>
{events_table(worst)}
<details><summary>Recent sentences (may overlap retained crossings)</summary>{events_table(g['recent'])}</details>
<p>Coverage: {g['excluded_async']} asynchronous dispatches excluded; {g['nested_dropped']} nested dispatches omitted;
{g['unassigned']} dispatches before a slot; {g['partial_uart']} partial-ingestion sentences; {g['uart_resets']} UART buffer resets.
Recent events overwritten: {g['recent_overwritten']} (capacity {g['recent_capacity']}); crossing capacity {g['worst_capacity']}.
Maximum profile-record bookkeeping: {g['record_max_us']} µs. This does not isolate full profiler overhead.</p>'''


def render(result):
    c, m, a = result["capture"], result["metadata"], result["assessment"]
    esc = lambda value: html.escape(str(value), quote=True)
    sim = c["source"] == "simulator"
    scale = max(1, max(s["max_occupied_us"] for s in c["slots"])) if sim else 10000
    cells = []
    for s in c["slots"]:
        used = s["max_occupied_us"]
        p95 = percentile_upper(s, c["histogram_upper_us"], .95)
        color = "unknown" if sim or not s["count"] else (
            "red" if s["deadline_misses"] or used > 10000 else
            "orange" if used >= 7500 else "yellow" if used >= 5000 else "green")
        detail = {k: v for k, v in s.items() if k != "histogram"}
        title = "; ".join(f"{k}: {v}" for k, v in detail.items())
        cells.append(f'<article class="cell {color}" title="{esc(title)}">'
                     f'<b>{s["slot"]:02d}</b> <span>{esc(s["label"])}</span>'
                     f'<div class="track"><i style="width:{min(100, used / scale * 100):.2f}%"></i></div>'
                     f'<small>max {used / 1000:.2f} ms · p95 ≤ {p95 / 1000:.2f} ms</small>'
                     f'<small>start +{s["max_start_delay_us"] / 1000:.2f} · end +{s["max_end_delay_us"] / 1000:.2f} ms</small>'
                     f'<small>avg T {s["tasks_us"] / s["count"] / 1000:.2f} G {s["gps_us"] / s["count"] / 1000:.2f} S {s["services_us"] / s["count"] / 1000:.2f}</small>'
                     f'<small>n={s["count"]} · misses {s["deadline_misses"]}</small>'
                     f'<small>excluded B {s.get("button_excluded", 0)} · R {s.get("recovery_excluded", 0)}</small></article>' if p95 is not None else
                     f'<article class="cell unknown"><b>{s["slot"]:02d}</b> {esc(s["label"])}<p>No samples</p></article>')
    notes = "".join(f"<li>{esc(n)}</li>" for n in a["notes"])
    changes = "".join(f'<tr><td>{s["slot"]:02d} {esc(s["label"])}</td><td>{s["max_delta_us"] / 1000:+.3f} ms</td>'
                      f'<td>{s["deadline_miss_rate_delta"] * 100:+.2f} percentage points</td></tr>'
                      for s in a["largest_changes"])
    trace_rows = []
    for kind, s in (("Normal preceding", c.get("worst_previous", {})), ("Normal worst", c.get("worst", {})),
                    ("Recovery preceding", c.get("worst_recovery_previous", {})), ("Recovery worst", c.get("worst_recovery", {}))):
        if not s.get("complete"):
            continue
        trace_rows.append(f'<tr><td>{kind}</td><td>{s["slot"]:02d}</td><td>{s["tick"]}</td>'
                          f'<td>{s["expected_us"]}</td><td>{s["start_us"]}</td><td>{s["tasks_end_us"]}</td>'
                          f'<td>{s["last_work_end_us"]}</td>'
                          f'<td>{s["tasks_us"]}/{s["gps_us"]}/{s["services_us"]}</td>'
                          f'<td>{s.get("display_page_mask", 0):02x}</td></tr>')
    mem = a["memory"]
    memory_html = '<p>Device heap and stack measurements unavailable.</p>'
    if mem:
        memory_html = (f'<p>Internal heap change {mem["internal_free_change"]:+,} B. '
                       f'Allocation failures: {esc(mem["failed_allocations"])}</p>'
                       '<p>Sampled minima: ' + esc(json.dumps(mem["sampled_minima"])) + '</p>')
    page_rows = []
    for p in sorted(a["pages"], key=lambda p: (p["count"] == 0, -p["max_render_us"])):
        if not p["count"]:
            page_rows.append(f'<tr><td>{esc(p["label"])}</td><td colspan="6">Not measured</td>'
                             f'<td>{p["button_excluded"]}/{p["recovery_excluded"]}</td></tr>')
            continue
        color = "unknown" if sim else ("red" if p["max_render_us"] > 10000 else
                 "orange" if p["max_render_us"] >= 7500 else "yellow" if p["max_render_us"] >= 5000 else "green")
        scale_render = max(1, max(q["max_render_us"] for q in a["pages"])) if sim else 10000
        page_rows.append(f'<tr><td>{esc(p["label"])}</td><td>{p["count"]}</td>'
                         f'<td class="{color}">{p["max_render_us"] / 1000:.3f}'
                         f'<div class="track"><i style="width:{min(100, p["max_render_us"] / scale_render * 100):.2f}%"></i></div></td>'
                         f'<td>{p["average_render_us"] / 1000:.3f}</td><td>≤{p["p95_render_upper_us"] / 1000:.3f}</td>'
                         f'<td>{p["max_slot_occupied_us"] / 1000:.3f}</td><td>{p["deadline_misses"]}/{p["slots"]}</td>'
                         f'<td>{p["button_excluded"]}/{p["recovery_excluded"]}</td></tr>')
    page_html = ('<p>Not available in this legacy capture.</p>' if not a["pages"] else
                 '<table><tr><th>Page</th><th>Renders</th><th>Max ms</th><th>Mean ms</th><th>p95 ms</th>'
                 '<th>Max occupied slot ms</th><th>Slot misses/samples</th><th>Excluded B/R renders</th></tr>' +
                 ''.join(page_rows) + '</table>')
    recovery_html = '<p>Not available in this legacy capture.</p>'
    if a["button_slots"] is not None:
        b, r = a["button_slots"], a["recovery_slots"]
        recovery_html = (f'<p>Excluded from normal grid: {b["count"]} button slots and {r["count"]} recovery slots. '
                         f'Button peak occupied {b["max_occupied_us"] / 1000:.3f} ms; '
                         f'button coalesced ticks {b["skipped_ticks"]}. Recovery: {r["deadline_misses"]} deadline crossings, '
                         f'{r["skipped_ticks"]} coalesced ticks; max start +{r["max_start_delay_us"] / 1000:.3f} ms, '
                         f'end +{r["max_end_delay_us"] / 1000:.3f} ms, '
                         f'barometer first call +{r["max_baro_start_delay_us"] / 1000:.3f} ms.</p>')
        if not b["count"]:
            recovery_html += '<p>No button actions observed; button recovery is untested.</p>'
        rows = []
        for s in c["slots"]:
            if s["button_excluded"] or s["recovery_excluded"]:
                rows.append(f'<tr><td>{s["slot"]:02d}</td><td>{s["button_excluded"]}/{s["recovery_excluded"]}</td>'
                            f'<td>{s["max_recovery_start_delay_us"] / 1000:.3f}</td>'
                            f'<td>{s["max_recovery_end_delay_us"] / 1000:.3f}</td>'
                            f'<td>{s["recovery_deadline_misses"]}/{s["recovery_skipped_ticks"]}</td>'
                            f'<td>{s["max_recovery_baro_start_delay_us"] / 1000:.3f}</td></tr>')
        if rows:
            recovery_html += ('<details><summary>Recovery by slot</summary><table><tr><th>Slot</th><th>Excluded B/R</th>'
                              '<th>Max start +ms</th><th>Max end +ms</th><th>Misses/ticks</th><th>Baro +ms</th></tr>' +
                              ''.join(rows) + '</table></details>')
    embedded = json.dumps(report_fields(result)).replace("<", "\\u003c").replace("&", "\\u0026")
    provenance = esc(json.dumps(m, sort_keys=True))
    legend = ("Host thread CPU time; gray bars scale to the largest observed host cost. No ESP32 timing estimate."
              if sim else "0–10 ms scale. Green <5; yellow 5–7.5; orange 7.5–10; red >10 ms or a deadline miss. Thresholds are provisional.")
    return f'''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Leaf runtime assessment — {esc(m.get('label', c['firmware']))}</title>
<style>
*{{box-sizing:border-box}}body{{font:14px system-ui,sans-serif;margin:24px;color:#172526;background:#f7f9f6}}
main{{max-width:1500px;margin:auto}}h1{{font-size:25px;margin-bottom:5px}}h2{{font-size:17px}}p{{line-height:1.5}}
.banner{{padding:12px;background:#fff0cb;border-left:5px solid #a76b00}}.grid{{display:grid;grid-template-columns:repeat(10,minmax(0,1fr));gap:5px}}
.cell{{background:white;border:1px solid #cbd2cf;border-top:4px solid var(--color);padding:6px;min-width:0}}
.cell span{{display:block;overflow-wrap:anywhere;font-size:11px;min-height:28px}}small{{display:block;font-size:10px;line-height:1.5}}
.green{{--color:#27804b}}.yellow{{--color:#c79500}}.orange{{--color:#de7228}}.red{{--color:#bd3337}}.unknown{{--color:#75868e}}
.track{{height:8px;background:#edf0ec;margin:5px 0}}.track i{{display:block;height:100%;background:var(--color)}}
table{{border-collapse:collapse;width:100%;font-size:12px}}td,th{{text-align:left;border-bottom:1px solid #d7deda;padding:5px}}li{{margin:5px 0}}code{{overflow-wrap:anywhere}}
.sections{{display:grid;grid-template-columns:1fr 1fr;gap:22px}}details{{margin-top:14px}}pre{{white-space:pre-wrap;overflow-wrap:anywhere}}
@media(max-width:850px){{body{{margin:10px}}.grid{{overflow-x:auto;grid-template-columns:repeat(10,110px)}}.sections{{grid-template-columns:1fr}}}}
@media print{{body{{margin:0;font-size:10px}}main{{max-width:none}}.cell{{padding:3px}}small{{font-size:8px}}.sections{{gap:10px}}h1{{font-size:19px}}details{{display:none}}}}
</style><main>
<h1>Leaf runtime assessment · {esc(m.get('label', c['firmware']))}</h1>
<p>{esc(c['source'])} · {esc(c['hardware'])} · {esc(c['firmware'])} · {a['duration_s']:.1f} seconds</p>
<p class="banner"><b>{esc(a['status'].replace('_', ' ').upper())}</b> · Normal slots: {a['deadline_misses']} deadline crossings ·
{a['duration_overruns']} duration overruns · {a['skipped_ticks']} coalesced ticks · minimum {a['minimum_slot_samples']} samples/slot</p>
<p>{esc(legend)} Bars show maxima across different seconds. A short red bar indicates work finishing beyond its deadline despite low accumulated occupancy. T/G/S show average assigned-task/GPS/service milliseconds. GPS and foreground services are included; empty UART polling is separate. Base work includes barometer, buttons, and speaker.</p>
<div class="grid">{''.join(cells)}</div>
<h2>Display pages</h2><p>Normal renders only; menus and overlays excluded. Render costs include SPI wait, drawing and transfer, and are already included in slot occupancy. Page flags refer to actual rendered content. Unvisited pages remain unmeasured.</p>{page_html}
<h2>Buttons and recovery</h2>{recovery_html}
{render_gps(c, a)}
<div class="sections"><section><h2>Memory</h2>{memory_html}
<h2>Observer footprint</h2><p>Capture buffer: {c.get('capture_bytes', 0):,} B ({'host' if sim else esc(c.get('capture_memory', 'unspecified'))}). Clock pair max: {c.get('clock_pair_max_us', 0)} µs;
slot bookkeeping max: {c.get('bookkeeping_max_us', 0)} µs; memory sample max: {c.get('memory_sample_max_us', 0)} µs.
Trace records overwritten: {c.get('trace_overwritten', 0)}; partial slots excluded: {c.get('partial_slots', 0)}.</p>
</section><section><h2>Baseline changes</h2>
<p>{'Compatible baseline' if a['baseline_comparable'] else 'No compatible baseline comparison'}</p>
<table><tr><th>Slot</th><th>Maximum change</th><th>Miss-rate change</th></tr>{changes}</table></section></div>
<h2>Worst incident and preceding slot</h2><p>Timestamps in microseconds since boot; work columns use {esc(c['work_clock'])} microseconds.</p>
<table><tr><th>Event</th><th>Slot</th><th>Tick</th><th>Expected start</th><th>Actual start</th><th>Tasks end</th><th>Last work end</th><th>Tasks/GPS/services</th><th>Page mask</th></tr>{''.join(trace_rows)}</table>
<details open><summary>Review notes and measurement limits</summary><ul>{notes}</ul></details>
<details><summary>Provenance</summary><pre>{provenance}</pre></details>
<script type="application/json" id="assessment-data">{embedded}</script></main></html>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="Output HTML; matching .json also written")
    parser.add_argument("--metadata", type=Path, help="JSON with commit, profile, scenario, toolchain and coverage notes")
    parser.add_argument("--firmware-binary", type=Path, help="Hash the exact firmware artifact used for this capture")
    parser.add_argument("--baseline", type=Path, help="Previous generated report JSON")
    args = parser.parse_args()
    try:
        if args.output.suffix.lower() != ".html":
            raise ValueError("Output must have an .html extension")
        inputs = {p.resolve() for p in (args.capture, args.metadata, args.firmware_binary, args.baseline) if p}
        if args.output.resolve() in inputs or args.output.with_suffix(".json").resolve() in inputs:
            raise ValueError("Report outputs must not overwrite raw evidence, metadata, firmware, or a baseline")
        raw = args.capture.read_bytes()
        capture = report_fields(json.loads(raw))
        metadata = json.loads(args.metadata.read_text(encoding="utf-8-sig")) if args.metadata else {}
        if not isinstance(metadata, dict):
            raise ValueError("Metadata must be a JSON object")
        if args.firmware_binary:
            metadata["firmware_sha256"] = hashlib.sha256(args.firmware_binary.read_bytes()).hexdigest()
        baseline = json.loads(args.baseline.read_text(encoding="utf-8")) if args.baseline else None
        result = {"report_schema_version": SCHEMA, "capture_sha256": hashlib.sha256(raw).hexdigest(),
                  "metadata": metadata, "capture": capture,
                  "assessment": assess(capture, metadata, baseline)}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(render(result), encoding="utf-8")
        args.output.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"{result['assessment']['status']}: {args.output}")
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(2, f"Cannot generate assessment: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
