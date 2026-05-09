# Attempt to make firmware binaries more reproducible by equalizing
# machine-specific strings and setting timestamps according to the commit.
#
# Does not equalize precompiled library strings in toolchain packages that may
# differ between host OS (Linux/Windows).

Import("env")

import os
import pathlib
import subprocess


def _run_git(args: list[str]) -> str | None:
    try:
        return subprocess.check_output(
            ["git", *args],
            cwd=env["PROJECT_DIR"],
            stderr=subprocess.STDOUT,
        ).decode(errors="replace").strip()
    except Exception:
        return None


def _path_forms(path: pathlib.Path) -> list[str]:
    resolved = path.resolve()
    forms = [str(resolved), resolved.as_posix()]
    # GCC diagnostics and __FILE__ strings on Windows commonly use forward
    # slashes even when Python/SCons starts with backslash paths.
    forms.append(str(resolved).replace("\\", "/"))
    return sorted(set(forms), key=len, reverse=True)


def _append_prefix_maps(source: pathlib.Path, target: str, flags: list[str]) -> None:
    for source_form in _path_forms(source):
        flags.append(f"-ffile-prefix-map={source_form}={target}")
        flags.append(f"-fmacro-prefix-map={source_form}={target}")


def _configure_source_date_epoch() -> None:
    # GCC uses SOURCE_DATE_EPOCH to make __DATE__ and __TIME__ deterministic.
    # Use the commit time so release builds still carry meaningful provenance.
    if os.environ.get("SOURCE_DATE_EPOCH"):
        return

    commit_time = _run_git(["log", "-1", "--format=%ct"])
    os.environ["SOURCE_DATE_EPOCH"] = commit_time if commit_time else "0"


def _configure_prefix_maps() -> None:
    flags: list[str] = []
    project_dir = pathlib.Path(env["PROJECT_DIR"])
    _append_prefix_maps(project_dir, "/leaf", flags)

    platformio_home = pathlib.Path.home() / ".platformio"
    if platformio_home.exists():
        _append_prefix_maps(platformio_home, "/platformio", flags)

    env.Append(CCFLAGS=flags)
    env.Append(CXXFLAGS=flags)

    print("[reproducible_build.py] SOURCE_DATE_EPOCH=" + os.environ["SOURCE_DATE_EPOCH"])
    print("[reproducible_build.py] Added path prefix maps for project and PlatformIO roots")


_configure_source_date_epoch()
_configure_prefix_maps()
