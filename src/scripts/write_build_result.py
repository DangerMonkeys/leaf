# tools/write_build_result.py
# Post-build summary: what changed/was produced in this build, timing, and roll-forward.
Import("env")
import os, json, pathlib, time, datetime, sys

BUILD_DIR = pathlib.Path(env.subst("$BUILD_DIR"))
DIAG_DIR  = BUILD_DIR / "diagnostics"
DIAG_DIR.mkdir(parents=True, exist_ok=True)

CUR_RES  = DIAG_DIR / "build_result.json"
PREV_RES = DIAG_DIR / "build_result.prev.json"

PRE_INFO = DIAG_DIR / "build_info.json"       # written by the pre script
PRE_INFO_PREV = DIAG_DIR / "build_info.prev.json"

PROGNAME = env.subst("${PROGNAME}")
ENV_NAME = env["PIOENV"]

# Common output names (some may not exist depending on platform/toolchain)
ARTIFACTS = [
    (BUILD_DIR / f"{PROGNAME}.elf"),
    (BUILD_DIR / f"{PROGNAME}.bin"),
    (BUILD_DIR / f"{PROGNAME}.map"),
    (BUILD_DIR / f"{PROGNAME}.hex"),
]

# Consider these file types for “touched this run”
FILE_EXTS = {".o", ".a", ".elf", ".bin", ".map", ".hex"}

def _stat(p: pathlib.Path):
    try:
        s = p.stat()
        return {
            "exists": True,
            "size": s.st_size,
            "mtime_epoch": s.st_mtime,
            "mtime_utc": datetime.datetime.utcfromtimestamp(s.st_mtime).isoformat(timespec="seconds") + "Z",
        }
    except FileNotFoundError:
        return {"exists": False}

def _now_utc_iso():
    return datetime.datetime.utcnow().isoformat(timespec="seconds") + "Z"

def _epoch(dt_iso: str):
    # Parse an ISO timestamp like "2025-08-15T17:02:03Z" to epoch; fallback to 0 on error
    try:
        if dt_iso.endswith("Z"):
            dt_iso = dt_iso[:-1]
        return datetime.datetime.fromisoformat(dt_iso).replace(tzinfo=datetime.timezone.utc).timestamp()
    except Exception:
        return 0.0

def _load_json(path: pathlib.Path):
    try:
        return json.loads(path.read_text())
    except Exception:
        return None

def summarize():
    # Establish build window: start from pre-snapshot (if present), end = now
    pre_info = _load_json(PRE_INFO)
    pre_prev = _load_json(PRE_INFO_PREV)

    start_iso = (pre_info or {}).get("timestamp_utc")
    start_epoch = _epoch(start_iso) if start_iso else 0.0
    end_epoch = time.time()
    end_iso = _now_utc_iso()
    duration_sec = max(0.0, end_epoch - start_epoch) if start_epoch else None

    # Gather final artifacts
    artifacts = {}
    for art in ARTIFACTS:
        artifacts[art.name] = _stat(art)

    # Collect files under this env’s build dir that were *touched this run* (mtime >= start)
    touched = []
    totals_by_type = {"o": 0, "a": 0, "elf": 0, "bin": 0, "map": 0, "hex": 0, "other": 0}
    size_by_type = {k: 0 for k in totals_by_type.keys()}

    # Simple categorization (directories)
    def categorize(path_rel_str: str):
        if path_rel_str.startswith("FrameworkArduino/"):
            return "framework"
        if path_rel_str.startswith("lib"):
            return "libdeps"
        # Any other subdir under the env build dir
        return "project_or_misc"

    counts_by_cat = {"framework": 0, "libdeps": 0, "project_or_misc": 0}
    size_by_cat   = {k: 0 for k in counts_by_cat.keys()}

    for p in BUILD_DIR.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix.lower() not in FILE_EXTS:
            continue
        try:
            st = p.stat()
        except FileNotFoundError:
            continue

        # If we have a start time, only include files modified during/after this run
        if start_epoch and st.st_mtime + 1e-6 < start_epoch:
            continue

        rel = p.relative_to(BUILD_DIR).as_posix()
        ext = p.suffix.lower().lstrip(".")
        kind = ext if ext in totals_by_type else "other"
        cat  = categorize(rel)

        rec = {
            "path": rel,
            "type": kind,
            "category": cat,   # framework / libdeps / project_or_misc
            "size": st.st_size,
            "mtime_epoch": st.st_mtime,
            "mtime_utc": datetime.datetime.utcfromtimestamp(st.st_mtime).isoformat(timespec="seconds") + "Z",
        }
        touched.append(rec)
        totals_by_type[kind] += 1
        size_by_type[kind] += st.st_size
        counts_by_cat[cat] += 1
        size_by_cat[cat] += st.st_size

    # Sort touched files for stable output
    touched.sort(key=lambda r: (r["category"], r["type"], r["path"]))

    # Pull over a few identifiers from pre-info so you can correlate runs
    id_block = {}
    for k in ("env_name", "board", "pio_platform", "pio_framework",
              "signature_stable_sha1", "signature_raw_sha1",
              "paths", "git"):
        if pre_info and k in pre_info:
            id_block[k] = pre_info[k]

    result = {
        "summary_version": 1,
        "generated_at_utc": end_iso,
        "environment": ENV_NAME,
        "build_window": {
            "start_utc": start_iso or None,
            "end_utc": end_iso,
            "duration_seconds": duration_sec,
        },
        "final_artifacts": artifacts,
        "touched_files": {
            "count": len(touched),
            "by_category_counts": counts_by_cat,
            "by_category_sizes": size_by_cat,
            "by_type_counts": totals_by_type,
            "by_type_sizes": size_by_type,
            "list": touched,  # detailed list (paths relative to the env build dir)
        },
        "identifiers": id_block,
        "notes": [
            "Files listed in touched_files have mtime >= build start (from capture_build_info.py).",
            "If build start was unavailable, the list may be empty or incomplete.",
            "Compare build_result.prev.json vs build_result.json to spot unusual bursts of work.",
        ],
    }

    # Rotate previous → prev.json, then write current (pretty, sorted)
    if CUR_RES.exists():
        PREV_RES.write_bytes(CUR_RES.read_bytes())
    CUR_RES.write_text(json.dumps(result, indent=2, sort_keys=True))

# Hook after main program binary; if your toolchain doesn’t emit .bin, hook .elf as fallback
def _run_summary(source, target, env):
    try:
        summarize()
        print(f"[build_result] Wrote {CUR_RES}")
    except Exception as e:
        print(f"[build_result] Failed to write summary: {e}")

# Prefer .bin, but also attach to .elf to be safe
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _run_summary)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _run_summary)
