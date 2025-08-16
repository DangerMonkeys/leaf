# tools/write_build_result.py
# Post-build summary (capture-only). Safe with both .elf and .bin post-actions.
Import("env")
import os, json, pathlib, time, datetime

BUILD_DIR = pathlib.Path(env.subst("$BUILD_DIR"))
DIAG_DIR  = BUILD_DIR / "diagnostics"
DIAG_DIR.mkdir(parents=True, exist_ok=True)

CUR_RES  = DIAG_DIR / "build_result.json"
PREV_RES = DIAG_DIR / "build_result.prev.json"

PRE_INFO = DIAG_DIR / "build_info.json"       # from the pre script
PRE_INFO_PREV = DIAG_DIR / "build_info.prev.json"

PROGNAME = env.subst("${PROGNAME}")
ENV_NAME = env["PIOENV"]

ARTIFACTS = [
    (BUILD_DIR / f"{PROGNAME}.elf"),
    (BUILD_DIR / f"{PROGNAME}.bin"),
    (BUILD_DIR / f"{PROGNAME}.map"),
    (BUILD_DIR / f"{PROGNAME}.hex"),
]
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

def _epoch(dt_iso: str | None):
    try:
        if not dt_iso:
            return 0.0
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

def _summarize():
    # Build window: start from pre-snapshot (if present), end = now
    pre_info = _load_json(PRE_INFO)
    start_iso = (pre_info or {}).get("timestamp_utc")
    start_epoch = _epoch(start_iso)
    end_epoch = time.time()
    end_iso = _now_utc_iso()
    duration_sec = max(0.0, end_epoch - start_epoch) if start_epoch else None

    # Final artifacts
    artifacts = {art.name: _stat(art) for art in ARTIFACTS}

    # Touched files since start
    touched = []
    totals_by_type = {"o": 0, "a": 0, "elf": 0, "bin": 0, "map": 0, "hex": 0, "other": 0}
    size_by_type   = {k: 0 for k in totals_by_type}
    counts_by_cat  = {"framework": 0, "libdeps": 0, "project_or_misc": 0}
    size_by_cat    = {k: 0 for k in counts_by_cat}

    def categorize(rel: str):
        if rel.startswith("FrameworkArduino/"):
            return "framework"
        if rel.startswith("lib"):
            return "libdeps"
        return "project_or_misc"

    for p in BUILD_DIR.rglob("*"):
        if not p.is_file():
            continue
        ext = p.suffix.lower()
        if ext not in FILE_EXTS:
            continue
        try:
            st = p.stat()
        except FileNotFoundError:
            continue
        if start_epoch and st.st_mtime + 1e-6 < start_epoch:
            continue

        rel = p.relative_to(BUILD_DIR).as_posix()
        kind = ext.lstrip(".") if ext.lstrip(".") in totals_by_type else "other"
        cat  = categorize(rel)

        rec = {
            "path": rel,
            "type": kind,
            "category": cat,
            "size": st.st_size,
            "mtime_epoch": st.st_mtime,
            "mtime_utc": datetime.datetime.utcfromtimestamp(st.st_mtime).isoformat(timespec="seconds") + "Z",
        }
        touched.append(rec)
        totals_by_type[kind] += 1
        size_by_type[kind]   += st.st_size
        counts_by_cat[cat]   += 1
        size_by_cat[cat]     += st.st_size

    touched.sort(key=lambda r: (r["category"], r["type"], r["path"]))

    # IDs copied from pre-info to correlate runs
    id_block = {}
    for k in ("env_name", "board", "pio_platform", "pio_framework",
              "signature_stable_sha1", "signature_raw_sha1",
              "paths", "git"):
        if pre_info and k in pre_info:
            id_block[k] = pre_info[k]

    return {
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
            "list": touched,
        },
        "identifiers": id_block,
        "notes": [
            "Files listed have mtime >= build start (from capture_build_info.py).",
            "If build start was unavailable, the list may be empty or incomplete.",
            "Compare build_result.prev.json vs build_result.json to spot unusual bursts of work.",
        ],
    }

def _write_single_run(result: dict):
    """Write build_result.json. Rotate to .prev ONLY if this is a new build (different start_utc)."""
    # Determine this run's start_utc
    start_utc = (result.get("build_window") or {}).get("start_utc")

    # If we already have a current result for the same build, overwrite without rotating
    if CUR_RES.exists():
        try:
            cur_existing = json.loads(CUR_RES.read_text())
        except Exception:
            cur_existing = {}
        existing_start = (cur_existing.get("build_window") or {}).get("start_utc")
        if existing_start == start_utc:
            # Same build → keep prev as the true previous build
            CUR_RES.write_text(json.dumps(result, indent=2, sort_keys=True))
            print(f"[build_result] Updated current summary (same build): {CUR_RES}")
            return

    # New build (or no existing file) → rotate current to prev, then write
    if CUR_RES.exists():
        PREV_RES.write_bytes(CUR_RES.read_bytes())
    CUR_RES.write_text(json.dumps(result, indent=2, sort_keys=True))
    print(f"[build_result] Wrote {CUR_RES} (rotated previous if it existed)")

def _should_write_for_target(target_node) -> bool:
    """Prefer writing on .bin. If this is .elf and a .bin exists for this build, skip."""
    try:
        tpath = pathlib.Path(str(target_node))
    except Exception:
        return True  # fail-open
    suffix = tpath.suffix.lower()

    if suffix == ".bin":
        return True

    if suffix == ".elf":
        # If bin already exists (produced in this run or previously) and is up-to-date vs build start, skip here.
        pre_info = _load_json(PRE_INFO)
        start_iso = (pre_info or {}).get("timestamp_utc")
        start_epoch = _epoch(start_iso)
        bin_path = BUILD_DIR / f"{PROGNAME}.bin"
        if bin_path.exists():
            try:
                m = bin_path.stat().st_mtime
                # If the .bin mtime is at/after the build start, prefer letting the .bin hook write.
                if start_epoch and m + 1e-6 >= start_epoch:
                    return False
            except Exception:
                pass
        # No .bin detected → allow .elf to write
        return True

    # Other target types: allow
    return True

def _run_summary(target, source, env):
    try:
        # If this callback is for .elf and a fresh .bin exists, let the .bin callback handle it.
        if not _should_write_for_target(target[0]):
            return
        result = _summarize()
        _write_single_run(result)
    except Exception as e:
        print(f"[build_result] Failed to write summary: {e}")

# Prefer .bin; also hook .elf as fallback
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _run_summary)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _run_summary)
