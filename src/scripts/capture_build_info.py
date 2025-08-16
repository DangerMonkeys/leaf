# tools/capture_build_info.py
# Capture a comprehensive, pretty-printed snapshot for diff-friendly troubleshooting.
Import("env")
import os, sys, json, hashlib, pathlib, platform, subprocess, datetime

BUILD_DIR = pathlib.Path(env.subst("$BUILD_DIR"))
DIAG_DIR  = BUILD_DIR / "diagnostics"
DIAG_DIR.mkdir(parents=True, exist_ok=True)

CUR_FP  = DIAG_DIR / "build_info.json"
PREV_FP = DIAG_DIR / "build_info.prev.json"

def _jsonable(x):
    try:
        json.dumps(x)
        return x
    except TypeError:
        return str(x)

def _norm_path(p: str) -> str:
    if p is None:
        return ""
    q = p.replace("\\", "/")
    if sys.platform.startswith("win"):
        q = q.lower()
    return q

def _realpath(p: str) -> str:
    try:
        return str(pathlib.Path(p).resolve())
    except Exception:
        return str(p)

def _count_objs(root: pathlib.Path) -> int:
    return sum(1 for _ in root.rglob("*.o")) if root.exists() else 0

def _sha256_text(s: str) -> str:
    return hashlib.sha256(s.encode("utf-8")).hexdigest()

def _maybe(cmd: list[str]) -> str | None:
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT)
        return out.decode(errors="replace").strip()
    except Exception:
        return None

def _git_info(project_dir: str) -> dict:
    info = {}
    try:
        cwd = os.getcwd()
        os.chdir(project_dir)
        info["root"]     = _maybe(["git", "rev-parse", "--show-toplevel"])
        info["head"]     = _maybe(["git", "rev-parse", "HEAD"])
        info["describe"] = _maybe(["git", "describe", "--tags", "--always", "--dirty"])
        info["status"]   = _maybe(["git", "status", "--porcelain"])
        if info["status"] is not None:
            info["dirty"] = (info["status"] != "")
            # For large repos, status could be long; provide a hash too.
            info["status_sha256"] = _sha256_text(info["status"])
        os.chdir(cwd)
    except Exception:
        pass
    return info

def _tool_versions() -> dict:
    # Try to grab compiler versions quickly (optional if missing)
    return {
        "python": platform.python_version(),
        "platform": platform.platform(),
        "platformio_core": _maybe(["pio", "--version"]),
        "gcc_version": _maybe([str(env.get("CC") or "gcc"), "--version"]),
        "gpp_version": _maybe([str(env.get("CXX") or "g++"), "--version"]),
        "ar_version":  _maybe([str(env.get("AR") or "ar"), "--version"]),
    }

def _package_versions(plat) -> dict:
    out = {}
    try:
        pkgs = [
            "framework-arduinoespressif32","framework-arduinoespressif8266","framework-arduino",
            "toolchain-xtensa-esp32","toolchain-xtensa-esp32s3",
            "toolchain-riscv32-esp","toolchain-gccarmnoneeabi",
            "tool-cmake","tool-ninja","tool-esptoolpy"
        ]
        for pkg in pkgs:
            v = plat.get_package_version(pkg)
            if v:
                out[pkg] = v
    except Exception:
        pass
    return out

def _collect():
    plat = env.PioPlatform()

    # Core identifiers
    env_name     = env["PIOENV"]
    project_dir  = str(env["PROJECT_DIR"])
    build_dir    = str(BUILD_DIR)

    # Paths (raw, normalized, realpath)
    paths = {
        "project_dir_raw": project_dir,
        "project_dir_norm": _norm_path(project_dir),
        "project_dir_real": _realpath(project_dir),
        "build_dir_raw": build_dir,
        "build_dir_norm": _norm_path(build_dir),
        "build_dir_real": _realpath(build_dir),
        "cwd_raw": os.getcwd(),
        "cwd_norm": _norm_path(os.getcwd()),
        "cwd_real": _realpath(os.getcwd()),
    }

    # Flags/defines/paths as seen by SCons
    snapshot = {
        "timestamp_utc": datetime.datetime.utcnow().isoformat(timespec="seconds") + "Z",
        "env_name": env_name,
        "board": _jsonable(env.get("BOARD")),
        "pio_platform": _jsonable(env.get("PIOPLATFORM")),
        "pio_framework": _jsonable(env.get("PIOFRAMEWORK")),
        "sconsflags": os.environ.get("SCONSFLAGS", ""),
        "build_flags_raw": _jsonable(env.get("BUILD_FLAGS")),
        "ccflags": _jsonable(env.get("CCFLAGS")),
        "cflags": _jsonable(env.get("CFLAGS")),
        "cxxflags": _jsonable(env.get("CXXFLAGS")),
        "linkflags": _jsonable(env.get("LINKFLAGS")),
        "cppdefines": _jsonable(env.get("CPPDEFINES")),
        "cpppath_norm": [_norm_path(str(p)) for p in (env.get("CPPPATH") or [])],
        "cpppath_raw":  [str(p) for p in (env.get("CPPPATH") or [])],
        "libpath_norm": [_norm_path(str(p)) for p in (env.get("LIBPATH") or [])],
        "libpath_raw":  [str(p) for p in (env.get("LIBPATH") or [])],
        "libs": _jsonable(env.get("LIBS")),
        "tools": _tool_versions(),
        "platform": {
            "name": str(plat),
            "version": getattr(plat, "version", None),
            "packages": _package_versions(plat),
        },
        "paths": paths,
        "preexisting_objects": {
            "framework_count": _count_objs(BUILD_DIR / "FrameworkArduino"),
            "lib_counts": { p.name: _count_objs(p) for p in BUILD_DIR.glob("lib*/") },
        },
        "git": _git_info(project_dir),
    }

    # Add quick signatures for fast comparisons
    # (stable uses normalized paths; raw uses raw strings)
    exclude = {"tools"}  # omit verbose tool --version strings from signature
    stable_basis = {k: snapshot[k] for k in snapshot if k not in exclude}
    raw_basis    = dict(stable_basis)
    # swap normalized with raw for raw signature
    raw_basis["paths"]["project_dir_norm"] = snapshot["paths"]["project_dir_raw"]
    raw_basis["paths"]["build_dir_norm"]   = snapshot["paths"]["build_dir_raw"]
    raw_basis["paths"]["cwd_norm"]         = snapshot["paths"]["cwd_raw"]
    raw_basis["cpppath_norm"] = snapshot["cpppath_raw"]
    raw_basis["libpath_norm"] = snapshot["libpath_raw"]

    snapshot["signature_stable_sha1"] = hashlib.sha1(json.dumps(stable_basis, sort_keys=True, default=str).encode()).hexdigest()
    snapshot["signature_raw_sha1"]    = hashlib.sha1(json.dumps(raw_basis,    sort_keys=True, default=str).encode()).hexdigest()

    return snapshot

def _write_snapshot(cur: dict):
    # rotate previous → prev.json
    if CUR_FP.exists():
        PREV_FP.write_bytes(CUR_FP.read_bytes())
    # write current (pretty, sorted keys)
    CUR_FP.write_text(json.dumps(cur, indent=2, sort_keys=True))

snap = _collect()
_write_snapshot(snap)
